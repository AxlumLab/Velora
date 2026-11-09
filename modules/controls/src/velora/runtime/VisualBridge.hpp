#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <velora/composition/VisualId.hpp>
#include <velora/globals.hpp>
#include <velora/runtime/ElementTree.hpp>

// Keeps a composition Visual tree in step with a ElementTree. Each element owns one
// visual; render properties map onto visual properties without repainting, and other
// paint-affecting properties re-run the element type's painter. Layout is not done here:
// Left/Top/Width/Height are the arranged rectangle written by the layout host. The bridge
// orders the compositor root's children to match ElementTree::roots().
//
// Only the visual identity is named here; Compositor and Canvas are forward-declared so this
// public header does not pull the composition implementation into its includers.
namespace velora
{

class Canvas;
class Compositor;

struct VisualProperties
{
    const PropertyDescriptor* opacity = nullptr;         // double 0..1
    const PropertyDescriptor* visible = nullptr;         // bool
    const PropertyDescriptor* zIndex = nullptr;          // int
    const PropertyDescriptor* left = nullptr;            // double, relative to the parent
    const PropertyDescriptor* top = nullptr;             // double
    const PropertyDescriptor* width = nullptr;           // double
    const PropertyDescriptor* height = nullptr;          // double
    const PropertyDescriptor* clipToBounds = nullptr;    // bool
    const PropertyDescriptor* background = nullptr;      // int, 0xAARRGGBB; 0 = none
    const PropertyDescriptor* cacheMode = nullptr;       // bool, retain as a composition layer
    const PropertyDescriptor* cornerRadius = nullptr;    // double, rounds the background
    const PropertyDescriptor* drawsBackground = nullptr; // bool; false when a template draws it

    static VisualProperties define(PropertyRegistry& registry);
};

struct VisualBridgeStats
{
    std::size_t created = 0, removed = 0, reordered = 0;
    std::size_t propertyUpdates = 0; // render properties applied without repaint
    std::size_t repaints = 0;
};

class VisualBridge
{
public:
    // Appends the element's content in local coordinates; the background is already drawn.
    using Painter = std::function<void(Canvas&, const ElementTree&, ElementId, float width, float height)>;

    VisualBridge(ElementTree& tree, Compositor& compositor, VisualProperties properties);
    ~VisualBridge();
    VisualBridge(const VisualBridge&) = delete;
    VisualBridge& operator=(const VisualBridge&) = delete;

    // Painters are chosen by the element's built-in type (R05); the string overloads serve trees
    // of untyped elements (tests, generic trees). A later registration replaces an earlier one.
    void setPainter(ElementKind kind, Painter painter);
    void setPainter(std::string elementType, Painter painter);
    // Content drawn above the element's children (scroll indicators), in the same local
    // coordinates and clip; repainted together with the element.
    void setOverlayPainter(ElementKind kind, Painter painter);
    void setOverlayPainter(std::string elementType, Painter painter);
    // Applies structure and property changes since the last sync. Call before snapshotting.
    void sync();
    VisualId visual(ElementId element) const;
    const VisualBridgeStats& stats() const noexcept
    {
        return m_stats;
    }

private:
    std::set<ElementId> reconcile(); // returns elements created
    void applyRenderProperties(ElementId element, VisualId visual);
    void repaint(ElementId element, VisualId visual);
    bool isRenderProperty(u64 propertyId) const;

    ElementTree& m_tree;
    Compositor& m_compositor;
    VisualProperties m_properties;
    std::unordered_map<std::string, Painter> m_painters, m_overlayPainters;
    std::unordered_map<ElementKind, Painter> m_kindPainters, m_kindOverlayPainters;
    const Painter* painterFor(ElementId element, bool overlay) const;
    std::unordered_map<ElementId, VisualId> m_overlays; // child visual above the element's children
    std::unordered_map<ElementId, VisualId> m_visuals;
    std::unordered_map<ElementId, VisualId> m_parents;
    std::set<std::pair<ElementId, u64>> m_pending; // (element, property id)
    u64 m_seenStructure = UINT64_MAX;
    std::size_t m_subscription = 0;
    VisualBridgeStats m_stats;
};

} // namespace velora
