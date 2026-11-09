#include "velora/runtime/VisualBridge.hpp"

#include <algorithm>
#include <limits>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/globals.hpp>
#include <velora/graphics/Canvas.hpp>

namespace velora
{
namespace
{

double number(const PropertyValue& v)
{
    if (v.isDouble())
        return v.asDouble();
    if (v.isInt())
        return static_cast<double>(v.asInt());
    return 0;
}

Color argb(i64 value)
{
    const auto channel = [&](int shift) { return static_cast<float>((value >> shift) & 0xFF) / 255.0f; };
    return {channel(16), channel(8), channel(0), channel(24)};
}

} // namespace

VisualProperties VisualProperties::define(PropertyRegistry& registry)
{
    using F = PropertyFlags;
    VisualProperties p;
    p.opacity = &registry.define("Opacity", PropertyValue(1.0), F::kPaint);
    p.visible = &registry.define("IsVisible", PropertyValue(true), F::kMeasure | F::kArrange | F::kPaint);
    p.zIndex = &registry.define("ZIndex", PropertyValue(i64{0}), F::kPaint);
    p.left = &registry.define("Left", PropertyValue(0.0), F::kArrange);
    p.top = &registry.define("Top", PropertyValue(0.0), F::kArrange);
    p.width = &registry.define("ActualWidth", PropertyValue(0.0), F::kArrange | F::kPaint);
    p.height = &registry.define("ActualHeight", PropertyValue(0.0), F::kArrange | F::kPaint);
    p.clipToBounds = &registry.define("ClipToBounds", PropertyValue(false), F::kPaint);
    p.background = &registry.define("Background", PropertyValue(i64{0}), F::kPaint);
    p.cacheMode = &registry.define("CacheMode", PropertyValue(false), F::kNone);
    p.cornerRadius = &registry.define("CornerRadius", PropertyValue(0.0), F::kPaint);
    p.drawsBackground = &registry.define("DrawsBackground", PropertyValue(true), F::kPaint);
    return p;
}

VisualBridge::VisualBridge(ElementTree& tree, Compositor& compositor, VisualProperties properties)
    : m_tree(tree), m_compositor(compositor), m_properties(properties)
{
    m_subscription = m_tree.subscribe([this](ElementId element, const PropertyDescriptor& d)
                                      { m_pending.emplace(element, d.id); });
}

VisualBridge::~VisualBridge()
{
    m_tree.unsubscribe(m_subscription);
}

void VisualBridge::setPainter(std::string elementType, Painter painter)
{
    m_painters[std::move(elementType)] = std::move(painter);
}

void VisualBridge::setOverlayPainter(std::string elementType, Painter painter)
{
    m_overlayPainters[std::move(elementType)] = std::move(painter);
}

void VisualBridge::setPainter(ElementKind kind, Painter painter)
{
    m_kindPainters[kind] = std::move(painter);
}

void VisualBridge::setOverlayPainter(ElementKind kind, Painter painter)
{
    m_kindOverlayPainters[kind] = std::move(painter);
}

const VisualBridge::Painter* VisualBridge::painterFor(ElementId element, bool overlay) const
{
    if (const auto kind = m_tree.kind(element))
    {
        const auto& painters = overlay ? m_kindOverlayPainters : m_kindPainters;
        if (const auto it = painters.find(*kind); it != painters.end())
            return &it->second;
    }
    // Untyped elements, and typed ones registered by type name.
    const auto& painters = overlay ? m_overlayPainters : m_painters;
    const auto it = painters.find(std::string(m_tree.type(element)));
    return it == painters.end() ? nullptr : &it->second;
}

VisualId VisualBridge::visual(ElementId element) const
{
    const auto it = m_visuals.find(element);
    return it == m_visuals.end() ? 0 : it->second;
}

bool VisualBridge::isRenderProperty(u64 id) const
{
    const auto& p = m_properties;
    return id == p.opacity->id || id == p.visible->id || id == p.zIndex->id || id == p.left->id ||
           id == p.top->id || id == p.clipToBounds->id || id == p.cacheMode->id;
}

void VisualBridge::sync()
{
    // New elements are fully painted by reconcile; their queued changes are already applied.
    const auto created = m_tree.structureVersion() != m_seenStructure ? reconcile() : std::set<ElementId>{};
    const auto pending = std::exchange(m_pending, {});
    // Several changed properties of one element cause at most one update and one repaint.
    std::set<ElementId> repaintSet, updateSet;
    for (const auto& [element, property] : pending)
    {
        if (!m_visuals.contains(element) || created.contains(element))
            continue;
        const bool size = property == m_properties.width->id || property == m_properties.height->id;
        if (isRenderProperty(property) || size)
            updateSet.insert(element); // size also moves the clip
        if (!isRenderProperty(property))
            repaintSet.insert(element);
    }
    for (const ElementId element : updateSet)
    {
        applyRenderProperties(element, m_visuals.at(element));
        ++m_stats.propertyUpdates;
    }
    for (const ElementId element : repaintSet)
        repaint(element, m_visuals.at(element));
}

std::set<ElementId> VisualBridge::reconcile()
{
    m_seenStructure = m_tree.structureVersion();
    std::unordered_map<ElementId, VisualId> live;
    std::set<ElementId> created;
    // Match the element order; z-index sorting happens in the compositor.
    const auto order = [&](VisualId parent, const std::vector<ElementId>& elements)
    {
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            const auto& current = m_compositor.children(parent);
            const VisualId wanted = live.at(elements[i]);
            if (i < current.size() && current[i] == wanted)
                continue;
            m_compositor.reorder(wanted, i);
            ++m_stats.reordered;
        }
    };
    // Depth-first so parents exist before children.
    const auto visit = [&](auto&& self, ElementId element, VisualId parentVisual) -> void
    {
        auto it = m_visuals.find(element);
        VisualId visual = it == m_visuals.end() ? 0 : it->second;
        if (visual && m_compositor.contains(visual) && m_parents[element] != parentVisual)
            m_compositor.remove(visual);
        if (visual && !m_compositor.contains(visual))
            visual = 0;
        m_parents[element] = parentVisual;
        if (!visual)
        {
            visual = m_compositor.createVisual(parentVisual);
            created.insert(element);
            ++m_stats.created;
        }
        live.emplace(element, visual);
        for (const ElementId child : m_tree.visualChildren(element))
            self(self, child, visual);
        order(visual, m_tree.visualChildren(element));
    };
    for (const ElementId root : m_tree.visualRoots())
        visit(visit, root, m_compositor.root());
    order(m_compositor.root(), m_tree.visualRoots());
    for (const auto& [element, visual] : m_visuals)
    {
        if (live.contains(element))
            continue;
        // Removing a visual removes its subtree; descendants may already be gone.
        if (m_compositor.contains(visual))
            m_compositor.remove(visual);
        ++m_stats.removed;
    }
    m_visuals = std::move(live);
    std::erase_if(m_overlays, [&](const auto& entry) { return !m_visuals.contains(entry.first); });
    for (const ElementId element : created)
    {
        applyRenderProperties(element, m_visuals.at(element));
        repaint(element, m_visuals.at(element));
    }
    return created;
}

void VisualBridge::applyRenderProperties(ElementId element, VisualId visual)
{
    const auto& p = m_properties;
    m_compositor.setOffset(visual, {static_cast<float>(number(m_tree.property(element, *p.left))),
                                    static_cast<float>(number(m_tree.property(element, *p.top)))});
    m_compositor.setOpacity(visual, static_cast<float>(number(m_tree.property(element, *p.opacity))));
    m_compositor.setVisible(visual, m_tree.property(element, *p.visible).asBool());
    m_compositor.setZIndex(visual, static_cast<int>(number(m_tree.property(element, *p.zIndex))));
    m_compositor.setLayer(visual, m_tree.property(element, *p.cacheMode).asBool());
    if (m_tree.property(element, *p.clipToBounds).asBool())
    {
        const auto w = static_cast<float>(number(m_tree.property(element, *p.width)));
        const auto h = static_cast<float>(number(m_tree.property(element, *p.height)));
        m_compositor.setClip(visual, {VisualClip::Kind::kRect, {0, 0, w, h}});
    }
    else
    {
        m_compositor.setClip(visual, {});
    }
}

void VisualBridge::repaint(ElementId element, VisualId visual)
{
    const auto width = static_cast<float>(number(m_tree.property(element, *m_properties.width)));
    const auto height = static_cast<float>(number(m_tree.property(element, *m_properties.height)));
    Canvas canvas;
    if (const auto background = m_tree.property(element, *m_properties.background).asInt();
        (background >> 24) & 0xFF && m_tree.property(element, *m_properties.drawsBackground).asBool())
    {
        const auto radius = static_cast<float>(number(m_tree.property(element, *m_properties.cornerRadius)));
        if (radius > 0)
            canvas.drawRoundedRect({0, 0, width, height}, radius, {argb(background)});
        else
            canvas.drawRect({0, 0, width, height}, {argb(background)});
    }
    const auto object = m_tree.object(element);
    const bool custom =
        object && static_cast<const UIElement&>(*object).renderOverride(canvas, {width, height});
    if (!custom)
        if (const Painter* painter = painterFor(element, false))
            (*painter)(canvas, m_tree, element, width, height);
    m_compositor.setContent(visual, std::move(canvas).finish());
    if (const Painter* overlayPainter = painterFor(element, true))
    {
        // Removing or recreating the element's visual removes this child with it.
        auto& overlay = m_overlays[element];
        if (!overlay || !m_compositor.contains(overlay))
        {
            overlay = m_compositor.createVisual(visual);
            m_compositor.setZIndex(overlay, std::numeric_limits<int>::max());
        }
        Canvas content;
        (*overlayPainter)(content, m_tree, element, width, height);
        m_compositor.setContent(overlay, std::move(content).finish());
    }
    ++m_stats.repaints;
}

} // namespace velora
