#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <velora/controls/UIElement.hpp>
#include <velora/data/ObjectRef.hpp>
#include <velora/globals.hpp>
#include <velora/runtime/property/DependencyObject.hpp>
#include <velora/runtime/property/PropertyTypes.hpp>

namespace velora
{

// Event identities; ElementTree::dispatch routes them by id.
struct EventDescriptor
{
    int id = 0;
    std::string owner, name, signature;
};

class EventRegistry
{
public:
    const EventDescriptor& define(std::string owner, std::string name, std::string signature);
    const EventDescriptor* find(std::string_view owner, std::string_view name) const;

private:
    int m_next = 1;
    std::unordered_map<std::string, EventDescriptor> m_values; // key "owner.name"
};

using ElementId = u64;

struct ElementTreeStats
{
    std::size_t nodes = 0;
    std::size_t propertyValues = 0;
    std::size_t handlers = 0;
    std::size_t approximateBytes = 0; // container capacities plus node payload; excludes allocator overhead
};

// The legacy retained UI tree (R03.6 status: a bridge over object-level properties).
//
// Every element is backed by a DependencyObject that owns the element's property values and their
// precedence. The tree holds a shared reference to it, links it to its logical parent for inherited
// properties and listens for its invalidations (dirty flags, observers). It no longer stores values
// itself, so there is exactly one effective-value source per element; an object handed out through
// object() keeps its values after the element is removed.
// Routed event data (R05.4, UWP RoutedEventArgs). OriginalSource is the element the event was
// raised on; Handled stops delivery to handlers that did not ask for handled events too.
struct RoutedEventArgs
{
    u64 originalSource = 0;
    bool handled = false;
};

class ElementTree final : private IPropertyInvalidation
{
public:
    using EventHandler = std::function<bool()>;
    // `sender` is the element whose handler runs (the bubbling position).
    using RoutedHandler = std::function<void(u64 sender, RoutedEventArgs& args)>;
    using PropertyChanged = std::function<void(ElementId, const PropertyDescriptor&)>;

    ElementTree() = default;
    ~ElementTree();
    ElementTree(const ElementTree&) = delete;
    ElementTree& operator=(const ElementTree&) = delete;

    ElementId create(std::string type, ElementId parent = 0);
    // Hosts an existing element object (created with createElement, configured off-tree) as a new
    // node. 0 when the parent is unknown or the element is already in a tree.
    ElementId insert(std::shared_ptr<UIElement> element, ElementId parent = 0);
    bool remove(ElementId id);
    bool contains(ElementId id) const noexcept
    {
        return m_nodes.contains(id);
    }
    ElementId parent(ElementId id) const;
    const std::vector<ElementId>& children(ElementId id) const;
    std::string_view type(ElementId id) const;
    bool reorder(ElementId id, std::size_t index);
    bool adopt(ElementId parent, ElementId child, usize index);
    void setVisualParent(ElementId child, ElementId parent);
    ElementId visualParent(ElementId child) const;
    std::vector<ElementId> visualChildren(ElementId parent) const;
    std::vector<ElementId> visualRoots() const;
    PropertyValue propertyAt(ElementId id, const PropertyDescriptor& property, PropertySource source) const;

    // Elements without a parent, in creation order.
    const std::vector<ElementId>& roots() const noexcept
    {
        return m_roots;
    }
    // Incremented by every create, remove and reorder.
    u64 structureVersion() const noexcept
    {
        return m_structureVersion;
    }

    // Stores the value in its source layer. Notifies only when the effective value changes.
    bool setProperty(ElementId id, const PropertyDescriptor& d, PropertyValue value, PropertySource source);
    // Removes one source layer; the effective value falls back to the next layer, parent or default.
    bool clearProperty(ElementId id, const PropertyDescriptor& d, PropertySource source);
    const PropertyValue& property(ElementId id, const PropertyDescriptor& d) const
    {
        const auto it = m_nodes.find(id);
        if (it == m_nodes.end())
            throw std::runtime_error("unknown element");
        return it->second.object->getValue(d);
    }
    std::optional<PropertySource> propertySource(ElementId id, const PropertyDescriptor& d) const;
    // The element's object; null for an unknown element. Shared: it may outlive the element.
    std::shared_ptr<DependencyObject> object(ElementId id) const;
    // The element's typed object (UIElement); null for an unknown element. Borrowed.
    UIElement* uiElement(ElementId id) const noexcept
    {
        const auto it = m_nodes.find(id);
        return it == m_nodes.end() ? nullptr : it->second.object.get();
    }
    // The element's built-in type, if it has one.
    std::optional<ElementKind> kind(ElementId id) const noexcept
    {
        const UIElement* element = uiElement(id);
        return element ? element->kind() : std::nullopt;
    }
    bool is(ElementId id, ElementKind kind) const noexcept
    {
        return this->kind(id) == kind;
    }
    // The element an object backs, or 0 when the object is not (or no longer) in this tree.
    ElementId element(const DependencyObject& object) const noexcept;
    // Observers are called after every effective value change; ids stay valid until unsubscribed.
    std::size_t subscribe(PropertyChanged callback);
    void unsubscribe(std::size_t subscription);

    bool measureDirty(ElementId id) const;
    bool arrangeDirty(ElementId id) const;
    bool paintDirty(ElementId id) const;
    void clearDirty(ElementId id);

    // Keeps `object` alive as long as the element: it is released after the element (and its
    // subtree) is removed, outside any tree traversal, so its destructor may use the tree.
    bool attach(ElementId id, std::shared_ptr<void> object);
    // Runs `callback` once the element has been removed, under the same rules as attach().
    bool onRemoved(ElementId id, std::function<void()> callback);

    bool on(ElementId id, int eventType, EventHandler handler);
    bool off(ElementId id, int eventType);
    // Independent registrations: tokens never repeat within this tree/Runtime.
    u64 addHandler(ElementId id, int eventType, EventHandler handler);
    bool removeHandler(ElementId id, u64 token);
    // A routed handler (UIElement.AddHandler): called while the event bubbles through `id`; with
    // handledEventsToo it also sees events an earlier handler marked Handled. Token as addHandler.
    u64 addRoutedHandler(ElementId id, int eventType, RoutedHandler handler, bool handledEventsToo = false);
    // Raises a routed event: bubbles from the target (or the pointer capture owner) to the root.
    // args.originalSource is set to the origin when empty. `classHandledAt` is an element on the
    // route whose class handling marks the event Handled before its instance handlers run (UWP
    // OnPointerPressed of ButtonBase and friends); 0 for none. Returns args.handled.
    bool raise(ElementId target, int eventType, RoutedEventArgs& args, ElementId classHandledAt = 0);
    // Raise with a fresh args; a bool handler returning true marks the event Handled.
    // Token callback exceptions are contained; legacy on() exceptions propagate as before.
    bool dispatch(ElementId target, int eventType);
    bool focus(ElementId id);
    ElementId focused() const noexcept
    {
        return m_focused;
    }
    bool capturePointer(ElementId id);
    void releasePointer() noexcept
    {
        m_capture = 0;
    }
    ElementId pointerCapture() const noexcept
    {
        return m_capture;
    }

    std::size_t node_count() const noexcept
    {
        return m_nodes.size();
    }
    ElementTreeStats stats() const;

private:
    struct HandlerRegistration
    {
        int type;
        u64 token;             // zero identifies the legacy replaceable slot
        EventHandler callback; // bool handlers: true marks the event Handled
        RoutedHandler routed;  // routed handlers (exactly one of the two is set)
        bool handledEventsToo = false;
        bool active = true;
    };
    struct Node
    {
        std::string type;
        ElementId parent = 0;
        std::vector<ElementId> children;
        std::shared_ptr<UIElement> object; // the typed element; owns its property values
        std::vector<std::shared_ptr<HandlerRegistration>> handlers;
        std::vector<std::shared_ptr<void>> attachments;
        bool measure = true, arrange = true, paint = true;
    };

    Node* find(ElementId id) noexcept;
    const Node* find(ElementId id) const noexcept;
    void mark(ElementId id, PropertyFlags flags);
    void markSubtree(ElementId id, PropertyFlags flags);
    // IPropertyInvalidation: an element's effective value changed.
    void invalidate(DependencyObject& object, const PropertyDescriptor& property) override;
    // Unlinks the object of a node that leaves the tree.
    void release(Node& node) noexcept;
    ElementId insertNode(std::string type, std::shared_ptr<UIElement> object, ElementId parent);

    ElementId m_next = 1;
    u64 m_nextHandler = 1;
    std::shared_ptr<bool> m_dispatchLifetime = std::make_shared<bool>(true);
    std::unordered_map<ElementId, Node> m_nodes;
    ElementId m_focused = 0, m_capture = 0;
    std::vector<ElementId> m_roots;
    std::unordered_map<ElementId, ElementId> m_visualParents;
    u64 m_structureVersion = 0;
    std::size_t m_nextSubscription = 1;
    std::vector<std::pair<std::size_t, PropertyChanged>> m_observers;
    std::unordered_map<const DependencyObject*, ElementId> m_elements;
};

// Realization window over up to kMaxItems logical rows; it never owns per-item elements.
class VirtualList final
{
public:
    static constexpr std::size_t kMaxItems = 100000;
    void setCount(std::size_t value)
    {
        count_ = std::min(value, kMaxItems);
        clamp();
    }
    void setRowHeight(float value)
    {
        m_rowHeight = value > 0 ? value : 1;
        setViewport(m_viewport);
    }
    void setViewport(float pixels)
    {
        m_viewport = std::max(0.0f, pixels);
        m_visible = static_cast<std::size_t>(m_viewport / m_rowHeight) + (m_viewport > 0 ? 1 : 0);
        clamp();
    }
    void scrollTo(std::size_t index)
    {
        m_first = index;
        clamp();
    }
    std::size_t count() const noexcept
    {
        return count_;
    }
    std::size_t firstVisible() const noexcept
    {
        return m_first;
    }
    std::size_t visibleCount() const noexcept
    {
        return std::min(m_visible, count_ - std::min(m_first, count_));
    }

private:
    void clamp()
    {
        if (m_first > count_)
            m_first = count_;
    }
    std::size_t count_ = 0, m_first = 0, m_visible = 0;
    float m_rowHeight = 1.0f, m_viewport = 0.0f;
};

} // namespace velora
