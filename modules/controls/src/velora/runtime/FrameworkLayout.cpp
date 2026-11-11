#include "velora/runtime/FrameworkInternal.hpp"

#include <velora/controls/LayoutContext.hpp>
#include <velora/globals.hpp>

namespace velora
{
// ---- Layout -------------------------------------------------------------------------

void Framework::update(float width, float height)
{
    // Realizing items after a viewport change dirties layout again; a few passes settle it.
    for (int pass = 0; pass < 4; ++pass)
    {
        layout(width, height);
        const bool lists = notifyViewports();
        if (!notifyViewChanged() && !lists)
            break;
    }
    m_bridge->sync();
}

bool Framework::notifyViewports()
{
    std::vector<ElementId> changed;
    for (auto& [id, list] : m_lists)
        if (list.callback && list.current != list.notified)
            changed.push_back(id);
    std::ranges::sort(changed);
    for (const ElementId id : changed)
    {
        const auto it = m_lists.find(id);
        if (it == m_lists.end())
            continue;
        it->second.notified = it->second.current;
        const auto callback = it->second.callback; // it may replace itself
        callback();
    }
    return !changed.empty();
}

void Framework::layout(float width, float height)
{
    bool dirty = width != m_width || height != m_height;
    for (const ElementId root : m_tree.visualRoots())
        dirty = dirty || m_tree.measureDirty(root) || m_tree.arrangeDirty(root);
    if (dirty)
    {
        m_width = width;
        m_height = height;
        for (const ElementId root : m_tree.visualRoots())
        {
            if (!m_tree.contains(root) || root == popupRoot())
                continue;
            measure(root, {width, height});
            arrange(root, {0, 0, width, height});
        }
        layoutPopup();
        const auto clear = [&](auto&& self, ElementId e) -> void
        {
            m_tree.clearDirty(e);
            for (const ElementId child : m_tree.visualChildren(e))
                self(self, child);
        };
        for (const ElementId root : m_tree.visualRoots())
            clear(clear, root);
    }
}

Size Framework::measure(ElementId e, Size available)
{
    if (!m_tree.property(e, *m_props.Visibility).asBool())
        return m_desired[e] = {};
    const Thickness margin = thickness(e, m_props.Margin);
    const float mx = static_cast<float>(margin.left + margin.right),
                my = static_cast<float>(margin.top + margin.bottom);
    Size inner{std::max(0.0f, available.width - mx), std::max(0.0f, available.height - my)};
    const double w = number(e, m_props.Width), h = number(e, m_props.Height);
    if (!std::isnan(w))
        inner.width = static_cast<float>(w);
    if (!std::isnan(h))
        inner.height = static_cast<float>(h);
    Size size = measureContent(e, inner);
    if (!std::isnan(w))
        size.width = static_cast<float>(w);
    if (!std::isnan(h))
        size.height = static_cast<float>(h);
    size.width = std::max(size.width, static_cast<float>(number(e, m_props.MinWidth)));
    size.height = std::max(size.height, static_cast<float>(number(e, m_props.MinHeight)));
    return m_desired[e] = {size.width + mx, size.height + my};
}

Size Framework::measureContent(ElementId e, Size available)
{
    const Thickness p = thickness(e, m_props.Padding), b = thickness(e, m_props.BorderThickness);
    MeasureInput input;
    input.available = available;
    input.insetX = static_cast<float>(p.left + p.right + b.left + b.right);
    input.insetY = static_cast<float>(p.top + p.bottom + b.top + b.bottom);
    input.content = {std::max(0.0f, available.width - input.insetX),
                     std::max(0.0f, available.height - input.insetY)};
    input.fontSize = static_cast<float>(number(e, m_props.FontSize));
    input.children = m_tree.visualChildren(e);
    LayoutContext context(*this);
    return m_tree.uiElement(e)->measureOverride(context, e, input);
}

void Framework::arrange(ElementId e, Box slot)
{
    const Size desired = m_desired[e];
    const Thickness margin = thickness(e, m_props.Margin);
    slot.x += static_cast<float>(margin.left);
    slot.y += static_cast<float>(margin.top);
    slot.width = std::max(0.0f, slot.width - static_cast<float>(margin.left + margin.right));
    slot.height = std::max(0.0f, slot.height - static_cast<float>(margin.top + margin.bottom));
    const float dw = std::max(0.0f, desired.width - static_cast<float>(margin.left + margin.right));
    const float dh = std::max(0.0f, desired.height - static_cast<float>(margin.top + margin.bottom));
    const auto align = [&](std::string_view a, bool explicitSize, float slotSize, float wanted, float& size)
    {
        const bool stretch = (a.empty() || a == "Stretch") && !explicitSize;
        size = stretch ? slotSize : std::min(wanted, slotSize);
        if (a == "Left" || a == "Top")
            return 0.0f;
        if (a == "Right" || a == "Bottom")
            return slotSize - size;
        return (slotSize - size) / 2;
    };
    float width = 0, height = 0;
    const float x = slot.x + align(text(e, m_props.HorizontalAlignment),
                                   !std::isnan(number(e, m_props.Width)), slot.width, dw, width);
    const float y = slot.y + align(text(e, m_props.VerticalAlignment), !std::isnan(number(e, m_props.Height)),
                                   slot.height, dh, height);
    set(e, m_visual.left, PropertyValue(static_cast<double>(x)));
    set(e, m_visual.top, PropertyValue(static_cast<double>(y)));
    set(e, m_visual.width, PropertyValue(static_cast<double>(width)));
    set(e, m_visual.height, PropertyValue(static_cast<double>(height)));
    if (m_tree.property(e, *m_props.HasTemplate).asBool())
    {
        arrangeContent(e, {0, 0, width, height}); // padding and border belong to the template
        return;
    }
    const Thickness p = thickness(e, m_props.Padding), b = thickness(e, m_props.BorderThickness);
    const float left = static_cast<float>(p.left + b.left), top = static_cast<float>(p.top + b.top);
    arrangeContent(e, {left, top, std::max(0.0f, width - left - static_cast<float>(p.right + b.right)),
                       std::max(0.0f, height - top - static_cast<float>(p.bottom + b.bottom))});
}

void Framework::arrangeContent(ElementId e, Box content)
{
    LayoutContext context(*this);
    m_tree.uiElement(e)->arrangeOverride(context, e, content, m_tree.visualChildren(e));
}

// ---- LayoutContext -----------------------------------------------------------------------------

Size LayoutContext::measure(ElementId child, Size available)
{
    return m_ui.measure(child, available);
}

void LayoutContext::arrange(ElementId child, Box slot)
{
    m_ui.arrange(child, slot);
}

Size LayoutContext::desired(ElementId child) const
{
    const auto it = m_ui.m_desired.find(child);
    return it == m_ui.m_desired.end() ? Size{} : it->second;
}

const PropertyValue& LayoutContext::property(ElementId element, const PropertyDescriptor* property) const
{
    return m_ui.m_tree.property(element, *property);
}

double LayoutContext::number(ElementId element, const PropertyDescriptor* property) const
{
    return m_ui.number(element, property);
}

std::string_view LayoutContext::text(ElementId element, const PropertyDescriptor* property) const
{
    return m_ui.text(element, property);
}

bool LayoutContext::isSet(ElementId element, const PropertyDescriptor* property) const
{
    return m_ui.m_tree.propertySource(element, *property).has_value();
}

bool LayoutContext::scrollable(ElementId element, const PropertyDescriptor* property) const
{
    return m_ui.scrollable(element, property);
}

float LayoutContext::measureText(std::string_view text, float fontSize) const
{
    return m_ui.m_measure(text, fontSize);
}

ElementId LayoutContext::contentObject(ElementId element) const
{
    return m_ui.contentObject(element);
}

void LayoutContext::set(ElementId element, const PropertyDescriptor* property, PropertyValue value)
{
    m_ui.set(element, property, std::move(value));
}

Framework::Viewport& LayoutContext::listViewport(ElementId list)
{
    return m_ui.m_lists[list].current;
}

Box Framework::bounds(ElementId e) const
{
    Box box{0, 0, static_cast<float>(number(e, m_visual.width)),
            static_cast<float>(number(e, m_visual.height))};
    for (ElementId cur = e; cur; cur = m_tree.visualParent(cur))
    {
        box.x += static_cast<float>(number(cur, m_visual.left));
        box.y += static_cast<float>(number(cur, m_visual.top));
    }
    return box;
}

ElementId Framework::hit(ElementId e, float x, float y, float originX, float originY) const
{
    if (!m_tree.property(e, *m_visual.visible).asBool())
        return 0;
    const float left = originX + static_cast<float>(number(e, m_visual.left)),
                top = originY + static_cast<float>(number(e, m_visual.top));
    const Box box{left, top, static_cast<float>(number(e, m_visual.width)),
                  static_cast<float>(number(e, m_visual.height))};
    if (m_tree.property(e, *m_visual.clipToBounds).asBool() && !box.contains(x, y))
        return 0;
    const auto& children = m_tree.visualChildren(e);
    for (auto it = children.rbegin(); it != children.rend(); ++it)
        if (const ElementId found = hit(*it, x, y, left, top))
            return found;
    return box.contains(x, y) ? e : 0;
}

ElementId Framework::hitTest(float x, float y) const
{
    if (m_popup && !m_popup->passive)
    {
        if (const auto target = hit(m_popup->root, x, y, 0, 0))
            return target;
        if (m_popup->passThrough && popupAnchorAvailable(m_popup->passThrough))
        {
            const auto parent = m_tree.visualParent(m_popup->passThrough);
            const auto origin = parent ? bounds(parent) : Box{};
            return hit(m_popup->passThrough, x, y, origin.x, origin.y);
        }
        return 0;
    }
    const auto& roots = m_tree.visualRoots();
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
    {
        if (m_popup && m_popup->passive && *it == m_popup->root)
            continue;
        if (const ElementId found = hit(*it, x, y, 0, 0))
            return found;
    }
    return 0;
}
} // namespace velora
