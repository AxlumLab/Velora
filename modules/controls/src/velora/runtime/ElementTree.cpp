#include "velora/runtime/ElementTree.hpp"

#include <iterator>
#include <velora/globals.hpp>

namespace velora
{

const EventDescriptor& EventRegistry::define(std::string owner, std::string name, std::string signature)
{
    std::string key = owner + "." + name;
    if (m_values.contains(key))
        throw std::runtime_error("duplicate event: " + key);
    EventDescriptor descriptor{m_next++, std::move(owner), std::move(name), std::move(signature)};
    return m_values.emplace(std::move(key), std::move(descriptor)).first->second;
}

const EventDescriptor* EventRegistry::find(std::string_view owner, std::string_view name) const
{
    const auto it = m_values.find(std::string(owner) + "." + std::string(name));
    return it == m_values.end() ? nullptr : &it->second;
}

ElementTree::Node* ElementTree::find(ElementId id) noexcept
{
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : &it->second;
}

const ElementTree::Node* ElementTree::find(ElementId id) const noexcept
{
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : &it->second;
}

ElementId ElementTree::create(std::string type, ElementId parentId)
{
    // The element's type picks its object class (R05): layout and behaviour are the type's own.
    auto object = createElementObject(findElementKind(type));
    return insertNode(std::move(type), std::move(object), parentId);
}

ElementId ElementTree::insert(std::shared_ptr<UIElement> element, ElementId parentId)
{
    if (!element || element->isInTree())
        return 0;
    std::string type = element->kind() ? std::string(schemaType(*element->kind()).name) : std::string();
    return insertNode(std::move(type), std::move(element), parentId);
}

ElementId ElementTree::insertNode(std::string type, std::shared_ptr<UIElement> object, ElementId parentId)
{
    Node* parentNode = nullptr;
    if (parentId != 0 && (parentNode = find(parentId)) == nullptr)
        return 0;
    const ElementId id = m_next++;
    if (parentNode)
        parentNode->children.push_back(id);
    object->setInheritanceParent(parentNode ? parentNode->object.get() : nullptr);
    object->setInvalidation(this);
    object->m_host = this;
    m_elements.emplace(object.get(), id);
    Node node{std::move(type), parentId};
    node.object = std::move(object);
    m_nodes.emplace(id, std::move(node));
    if (!parentId)
        m_roots.push_back(id);
    ++m_structureVersion;
    if (parentId)
        mark(parentId, PropertyFlags::kMeasure | PropertyFlags::kArrange | PropertyFlags::kPaint);
    return id;
}

bool ElementTree::remove(ElementId id)
{
    Node* node = find(id);
    if (!node)
        return false;
    if (node->parent)
    {
        auto& siblings = m_nodes.at(node->parent).children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
        mark(node->parent, PropertyFlags::kMeasure | PropertyFlags::kArrange | PropertyFlags::kPaint);
    }
    else
    {
        std::erase(m_roots, id);
    }
    ++m_structureVersion;
    std::vector<std::shared_ptr<void>> released;
    std::vector<ElementId> pending{id};
    while (!pending.empty())
    {
        const ElementId current = pending.back();
        pending.pop_back();
        const auto it = m_nodes.find(current);
        pending.insert(pending.end(), it->second.children.begin(), it->second.children.end());
        std::ranges::move(it->second.attachments, std::back_inserter(released));
        for (auto& registration : it->second.handlers)
        {
            registration->active = false;
            released.push_back(std::move(registration));
        }
        if (m_focused == current)
            m_focused = 0;
        if (m_capture == current)
            m_capture = 0;
        m_visualParents.erase(current);
        release(it->second);
        released.push_back(std::move(it->second.object));
        m_nodes.erase(it);
    }
    released.clear(); // attachments go last, with the tree consistent again
    return true;
}

ElementId ElementTree::parent(ElementId id) const
{
    const Node* node = find(id);
    return node ? node->parent : 0;
}

const std::vector<ElementId>& ElementTree::children(ElementId id) const
{
    static const std::vector<ElementId> empty;
    const Node* node = find(id);
    return node ? node->children : empty;
}

std::string_view ElementTree::type(ElementId id) const
{
    const Node* node = find(id);
    return node ? std::string_view(node->type) : std::string_view();
}

bool ElementTree::reorder(ElementId id, std::size_t index)
{
    const Node* node = find(id);
    if (!node || !node->parent)
        return false;
    auto& list = m_nodes.at(node->parent).children;
    const auto pos = std::find(list.begin(), list.end(), id);
    list.erase(pos);
    list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(index, list.size())), id);
    mark(node->parent, PropertyFlags::kArrange | PropertyFlags::kPaint);
    ++m_structureVersion;
    return true;
}

bool ElementTree::adopt(ElementId parentId, ElementId childId, usize index)
{
    auto* owner = find(parentId);
    auto* child = find(childId);
    if (!owner || !child || child->parent || index > owner->children.size())
        return false;
    for (auto current = parentId; current; current = parent(current))
        if (current == childId)
            return false;
    owner->children.insert(owner->children.begin() + static_cast<isize>(index), childId);
    child->parent = parentId;
    child->object->setInheritanceParent(owner->object.get());
    std::erase(m_roots, childId);
    m_visualParents.erase(childId);
    ++m_structureVersion;
    mark(parentId, PropertyFlags::kMeasure | PropertyFlags::kArrange | PropertyFlags::kPaint);
    return true;
}
void ElementTree::setVisualParent(ElementId child, ElementId parentId)
{
    if (!contains(child))
        return;
    if (visualParent(child) == parentId)
        return;
    m_visualParents.insert_or_assign(child, parentId);
    ++m_structureVersion;
    mark(child, PropertyFlags::kMeasure | PropertyFlags::kArrange | PropertyFlags::kPaint);
}
ElementId ElementTree::visualParent(ElementId child) const
{
    if (auto it = m_visualParents.find(child); it != m_visualParents.end())
        return it->second;
    return parent(child);
}
std::vector<ElementId> ElementTree::visualChildren(ElementId owner) const
{
    std::vector<ElementId> result;
    for (auto child : children(owner))
        if (visualParent(child) == owner)
            result.push_back(child);
    for (const auto& [child, visualOwner] : m_visualParents)
        if (visualOwner == owner && parent(child) != owner && contains(child))
            result.push_back(child);
    return result;
}
std::vector<ElementId> ElementTree::visualRoots() const
{
    std::vector<ElementId> result;
    for (auto root : m_roots)
        if (!visualParent(root))
            result.push_back(root);
    return result;
}
PropertyValue ElementTree::propertyAt(ElementId id, const PropertyDescriptor& d, PropertySource source) const
{
    if (const auto* node = find(id))
        if (const auto* value = node->object->layerValue(d, source))
            return *value;
    return {};
}

std::shared_ptr<DependencyObject> ElementTree::object(ElementId id) const
{
    const Node* node = find(id);
    return node ? node->object : nullptr;
}

ElementId ElementTree::element(const DependencyObject& object) const noexcept
{
    const auto it = m_elements.find(&object);
    return it == m_elements.end() ? 0 : it->second;
}

std::size_t ElementTree::subscribe(PropertyChanged callback)
{
    const std::size_t id = m_nextSubscription++;
    m_observers.emplace_back(id, std::move(callback));
    return id;
}

void ElementTree::unsubscribe(std::size_t subscription)
{
    std::erase_if(m_observers, [&](const auto& entry) { return entry.first == subscription; });
}

std::optional<PropertySource> ElementTree::propertySource(ElementId id, const PropertyDescriptor& d) const
{
    const Node* node = find(id);
    return node ? node->object->valueSource(d) : std::nullopt;
}

bool ElementTree::setProperty(ElementId id, const PropertyDescriptor& d, PropertyValue value,
                              PropertySource source)
{
    Node* node = find(id);
    if (!node)
        return false;
    // Keep the object alive across notification: an observer may remove the element.
    const auto object = node->object;
    // The tree is the internal bridge of the runtime that owns the element, so it writes
    // read-only runtime state through the descriptor as before.
    return object->setValue(d, std::move(value), source) == PropertySetResult::kOk;
}

bool ElementTree::clearProperty(ElementId id, const PropertyDescriptor& d, PropertySource source)
{
    Node* node = find(id);
    if (!node)
        return false;
    const auto object = node->object;
    return object->clearValue(d, source);
}

void ElementTree::invalidate(DependencyObject& object, const PropertyDescriptor& d)
{
    const ElementId id = element(object);
    if (!id)
        return;
    mark(id, d.flags);
    if (d.inherited)
    {
        for (const ElementId child : m_nodes.at(id).children)
            markSubtree(child, d.flags);
    }
    for (const auto& [subscription, observer] : m_observers)
        observer(id, d);
}

void ElementTree::release(Node& node) noexcept
{
    if (!node.object)
        return;
    node.object->setInvalidation(nullptr);
    node.object->setInheritanceParent(nullptr);
    node.object->m_host = nullptr;
    m_elements.erase(node.object.get());
}

void ElementTree::mark(ElementId id, PropertyFlags flags)
{
    for (Node* node = find(id); node; node = find(node->parent))
    {
        node->measure |= hasFlag(flags, PropertyFlags::kMeasure);
        node->arrange |= hasFlag(flags, PropertyFlags::kArrange);
        node->paint |= hasFlag(flags, PropertyFlags::kPaint);
    }
}

void ElementTree::markSubtree(ElementId id, PropertyFlags flags)
{
    std::vector<ElementId> pending{id};
    while (!pending.empty())
    {
        Node& node = m_nodes.at(pending.back());
        pending.pop_back();
        node.measure |= hasFlag(flags, PropertyFlags::kMeasure);
        node.arrange |= hasFlag(flags, PropertyFlags::kArrange);
        node.paint |= hasFlag(flags, PropertyFlags::kPaint);
        pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
}

bool ElementTree::measureDirty(ElementId id) const
{
    const Node* node = find(id);
    return node && node->measure;
}

bool ElementTree::arrangeDirty(ElementId id) const
{
    const Node* node = find(id);
    return node && node->arrange;
}

bool ElementTree::paintDirty(ElementId id) const
{
    const Node* node = find(id);
    return node && node->paint;
}

void ElementTree::clearDirty(ElementId id)
{
    if (Node* node = find(id))
        node->measure = node->arrange = node->paint = false;
}

ElementTree::~ElementTree()
{
    *m_dispatchLifetime = false;
    std::vector<std::shared_ptr<void>> released;
    std::vector<std::shared_ptr<DependencyObject>> objects;
    for (auto& [id, node] : m_nodes)
    {
        release(node);
        objects.push_back(std::move(node.object));
        std::ranges::move(node.attachments, std::back_inserter(released));
        for (auto& registration : node.handlers)
        {
            registration->active = false;
            released.push_back(std::move(registration));
        }
    }
    m_nodes.clear();
    m_roots.clear();
    m_focused = m_capture = 0;
    released.clear();
    objects.clear();
}

bool ElementTree::attach(ElementId id, std::shared_ptr<void> object)
{
    Node* node = find(id);
    if (!node)
        return false;
    node->attachments.push_back(std::move(object));
    return true;
}

bool ElementTree::onRemoved(ElementId id, std::function<void()> callback)
{
    struct Callback
    {
        explicit Callback(std::function<void()> f) : run(std::move(f)) {}
        Callback(const Callback&) = delete;
        Callback& operator=(const Callback&) = delete;
        ~Callback()
        {
            run();
        }
        std::function<void()> run;
    };
    return contains(id) && attach(id, std::make_shared<Callback>(std::move(callback)));
}

bool ElementTree::on(ElementId id, int eventType, EventHandler handler)
{
    Node* node = find(id);
    if (!node)
        return false;
    auto& handlers = node->handlers;
    auto registration =
        std::make_shared<HandlerRegistration>(HandlerRegistration{eventType, 0, std::move(handler)});
    const auto it = std::find_if(handlers.begin(), handlers.end(),
                                 [&](const auto& h) { return h->type == eventType && h->token == 0; });
    if (it != handlers.end())
    {
        (*it)->active = false;
        std::swap(*it, registration); // release the previous callback after storage is consistent
    }
    else
    {
        const auto firstToken =
            std::find_if(handlers.begin(), handlers.end(), [](const auto& h) { return h->token != 0; });
        handlers.insert(firstToken, std::move(registration));
    }
    return true;
}

bool ElementTree::off(ElementId id, int eventType)
{
    Node* node = find(id);
    if (!node)
    {
        return false;
    }
    auto& handlers = node->handlers;
    const auto it = std::find_if(handlers.begin(), handlers.end(),
                                 [&](const auto& h) { return h->type == eventType && h->token == 0; });
    if (it == handlers.end())
    {
        return false;
    }
    auto removed = *it;
    removed->active = false;
    handlers.erase(it);
    return true;
}

u64 ElementTree::addHandler(ElementId id, int eventType, EventHandler handler)
{
    Node* node = find(id);
    if (!node || !handler || m_nextHandler == 0)
    {
        return 0;
    }
    const u64 token = m_nextHandler;
    node->handlers.push_back(
        std::make_shared<HandlerRegistration>(HandlerRegistration{eventType, token, std::move(handler)}));
    ++m_nextHandler; // exhaustion leaves zero, so no token is ever reused
    return token;
}

bool ElementTree::removeHandler(ElementId id, u64 token)
{
    Node* node = find(id);
    if (!node || token == 0)
    {
        return false;
    }
    auto& handlers = node->handlers;
    const auto it =
        std::find_if(handlers.begin(), handlers.end(), [&](const auto& h) { return h->token == token; });
    if (it == handlers.end())
    {
        return false;
    }
    auto removed = *it;
    removed->active = false;
    handlers.erase(it);
    return true;
}

u64 ElementTree::addRoutedHandler(ElementId id, int eventType, RoutedHandler handler, bool handledEventsToo)
{
    Node* node = find(id);
    if (!node || !handler || m_nextHandler == 0)
        return 0;
    const u64 token = m_nextHandler;
    auto registration = std::make_shared<HandlerRegistration>(HandlerRegistration{eventType, token, {}});
    registration->routed = std::move(handler);
    registration->handledEventsToo = handledEventsToo;
    node->handlers.push_back(std::move(registration));
    ++m_nextHandler;
    return token;
}

bool ElementTree::dispatch(ElementId target, int eventType)
{
    RoutedEventArgs args;
    return raise(target, eventType, args);
}

bool ElementTree::raise(ElementId target, int eventType, RoutedEventArgs& args, ElementId classHandledAt)
{
    const auto lifetime = m_dispatchLifetime;
    const ElementId origin = m_capture ? m_capture : target;
    if (!args.originalSource)
        args.originalSource = origin;
    struct RouteEntry
    {
        ElementId id;
        std::vector<std::shared_ptr<HandlerRegistration>> handlers;
    };
    // The route is fixed when the event is raised; handlers added meanwhile wait for the next one.
    std::vector<RouteEntry> route;
    for (ElementId id = origin; const Node* node = find(id); id = node->parent)
        route.push_back({id, node->handlers});
    for (const auto& entry : route)
    {
        if (entry.id == classHandledAt)
            args.handled = true;
        for (const auto& registration : entry.handlers)
        {
            if (!*lifetime || !contains(origin))
                return args.handled;
            if (!contains(entry.id))
                break;
            if (!registration->active || registration->type != eventType)
                continue;
            if (args.handled && !registration->handledEventsToo)
                continue;
            if (registration->token == 0)
            {
                // Legacy replaceable slot: exceptions propagate as before.
                if (registration->callback())
                    args.handled = true;
                continue;
            }
            try
            {
                if (registration->routed)
                    registration->routed(entry.id, args);
                else if (registration->callback())
                    args.handled = true;
            }
            catch (...)
            {
                // One failing token callback does not prevent the remaining delivery.
            }
        }
    }
    return args.handled;
}

bool ElementTree::focus(ElementId id)
{
    if (id != 0 && !contains(id))
        return false;
    m_focused = id;
    return true;
}

bool ElementTree::capturePointer(ElementId id)
{
    if (!contains(id))
        return false;
    m_capture = id;
    return true;
}

ElementTreeStats ElementTree::stats() const
{
    ElementTreeStats result;
    result.nodes = m_nodes.size();
    result.approximateBytes = sizeof(*this) + m_nodes.bucket_count() * sizeof(void*);
    for (const auto& [id, node] : m_nodes)
    {
        result.propertyValues += node.object->store().size();
        result.handlers += node.handlers.size();
        result.approximateBytes += sizeof(std::pair<const ElementId, Node>) + sizeof(void*) * 2 +
                                   (node.type.capacity() > 15 ? node.type.capacity() + 1 : 0) +
                                   node.children.capacity() * sizeof(ElementId) + sizeof(DependencyObject) +
                                   node.object->store().approximateBytes() +
                                   node.handlers.capacity() * sizeof(std::shared_ptr<HandlerRegistration>) +
                                   node.handlers.size() * sizeof(HandlerRegistration);
    }
    return result;
}

} // namespace velora
