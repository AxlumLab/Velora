#include <cmath>
#include <limits>
#include <utility>
#include <velora/runtime/Framework.hpp>

namespace velora
{
ElementId Framework::pageOwner(ElementId element) const
{
    if (auto it = m_detachedPages.find(element); it != m_detachedPages.end())
        return it->second;
    for (auto current = element; current; current = m_tree.parent(current))
        if (m_tree.is(current, ElementKind::kPage))
            return current;
    return 0;
}
ElementId Framework::createDetached(ElementKind kind, ElementId anchor)
{
    if (!isOwnerThread() || !m_tree.contains(anchor) || kind == ElementKind::kPage)
        return 0;
    const auto page = pageOwner(anchor);
    if (!page)
        return 0;
    const auto id = create(kind);
    try
    {
        m_detachedPages.emplace(id, page);
        m_tree.setVisualParent(id, std::numeric_limits<ElementId>::max());
        m_tree.onRemoved(id, [this, id] { m_detachedPages.erase(id); });
        m_tree.onRemoved(page,
                         [this, id]
                         {
                             if (m_detachedPages.contains(id))
                                 m_tree.remove(id);
                         });
    }
    catch (...)
    {
        m_tree.remove(id);
        throw;
    }
    return id;
}
namespace
{
// Visual parent of unmounted top-level elements: no element has this id, so the element is not a
// visual root and nothing reaches it from one.
constexpr ElementId kUnmounted = std::numeric_limits<ElementId>::max();
} // namespace

bool Framework::isWithin(ElementId element, ElementId ancestor) const
{
    for (auto current = element; current; current = m_tree.parent(current))
        if (current == ancestor)
            return true;
    return false;
}

bool Framework::isMounted(ElementId element) const
{
    return m_tree.contains(element) && m_tree.visualParent(element) != kUnmounted;
}

bool Framework::setMounted(ElementId element, bool mounted)
{
    if (!isOwnerThread() || !m_tree.contains(element) || m_tree.parent(element))
        return false;
    if (mounted == isMounted(element))
        return true;
    if (mounted)
    {
        m_tree.setVisualParent(element, 0);
        return true;
    }
    // Input state of the subtree goes away with it; a TextBox composition is committed by focus().
    if (isWithin(m_tree.focused(), element))
        focus(0);
    if (isWithin(m_tree.pointerCapture(), element))
        m_tree.releasePointer();
    // Hover and press also show as properties (visual states), so they are reset, not just forgotten.
    if (m_hovered && isWithin(m_hovered, element))
        set(std::exchange(m_hovered, 0), m_props.IsPointerOver, PropertyValue(false));
    if (m_pressed && isWithin(m_pressed, element))
        set(std::exchange(m_pressed, 0), m_props.IsPressed, PropertyValue(false));
    for (ElementId* state : {&m_scrollHover, &m_sliderDrag, &m_scrollDrag.viewer})
        if (*state && isWithin(*state, element))
            *state = 0;
    m_tree.setVisualParent(element, kUnmounted);
    return true;
}

bool Framework::insertChild(ElementId parent, ElementId child, usize index)
{
    if (!isOwnerThread() || !m_tree.contains(parent) || !m_tree.contains(child) || !pageOwner(parent) ||
        pageOwner(parent) != pageOwner(child) || m_tree.parent(child))
        return false;
    if (!m_tree.adopt(parent, child, index))
        return false;
    m_detachedPages.erase(child);
    return true;
}
ElementId Framework::contentObject(ElementId owner) const
{
    if (!m_tree.contains(owner))
        return 0;
    const auto& value = m_tree.property(owner, *m_props.Content);
    return value.isElement() && !value.asElement().runtime.expired() &&
                   m_tree.contains(value.asElement().element)
               ? value.asElement().element
               : 0;
}
bool Framework::setContentObject(ElementId owner, ElementId child, PropertySource source)
{
    if (!isOwnerThread() || !m_tree.contains(owner))
        return false;
    if (!child)
    {
        clear(owner, m_props.Content, source);
        return true;
    }
    const PropertyValue assigned(ElementContentRef{child, lifetime()});
    if (auto it = m_templateRoots.find(owner); it != m_templateRoots.end() && it->second == child)
        return false;
    if (m_tree.propertyAt(owner, *m_props.Content, source) == assigned)
        return true;
    if (!m_tree.contains(child) || (m_tree.parent(child) && m_tree.parent(child) != owner) ||
        !pageOwner(owner) || pageOwner(owner) != pageOwner(child))
        return false;
    for (auto current = owner; current; current = m_tree.parent(current))
        if (current == child)
            return false;
    if (!m_tree.parent(child) && !insertChild(owner, child, m_tree.children(owner).size()))
        return false;
    setContentValue(owner, assigned, source);
    return true;
}
void Framework::set(ElementId owner, const PropertyDescriptor* property, PropertyValue value,
                    PropertySource source)
{
    if (!property)
        return;
    if (property == m_props.Content)
    {
        setContentValue(owner, std::move(value), source);
        return;
    }
    m_tree.setProperty(owner, *property, std::move(value), source);
}
void Framework::setContentValue(ElementId owner, PropertyValue value, PropertySource source)
{
    std::vector<ElementId> replaced;
    const auto kind = m_tree.kind(owner);
    if (kind == ElementKind::kBorder || kind == ElementKind::kPage || kind == ElementKind::kUserControl ||
        kind == ElementKind::kScrollViewer)
    {
        for (const auto child : m_tree.children(owner))
        {
            if ((value.isElement() && value.asElement().element == child) ||
                (m_templateRoots.contains(owner) && m_templateRoots.at(owner) == child))
                continue;
            bool retained = false;
            for (auto rank : kPropertySources)
            {
                const auto stored = m_tree.propertyAt(owner, *m_props.Content, rank);
                retained |= stored.isElement() && stored.asElement().element == child;
            }
            if (!retained)
                replaced.push_back(child);
        }
    }
    const auto previous = m_tree.propertyAt(owner, *m_props.Content, source);
    m_tree.setProperty(owner, *m_props.Content, std::move(value), source);
    for (auto child : replaced)
        m_tree.remove(child);
    if (previous.isElement() && m_tree.parent(previous.asElement().element) == owner)
    {
        bool retained = false;
        for (auto rank : kPropertySources)
            retained |= m_tree.propertyAt(owner, *m_props.Content, rank) == previous;
        if (!retained)
            m_tree.remove(previous.asElement().element);
    }
    contentChanged(owner);
}
void Framework::clear(ElementId owner, const PropertyDescriptor* property, PropertySource source)
{
    if (!property || !m_tree.contains(owner))
        return;
    if (auto it = m_themed.find(owner); it != m_themed.end())
        std::erase_if(it->second,
                      [&](const ThemedValue& v) { return v.property == property && v.source == source; });
    const auto previous = m_tree.propertyAt(owner, *property, source);
    m_tree.clearProperty(owner, *property, source);
    if (property == m_props.Content && previous.isElement() &&
        m_tree.parent(previous.asElement().element) == owner)
    {
        bool retained = false;
        for (auto rank : kPropertySources)
            retained |= m_tree.propertyAt(owner, *property, rank) == previous;
        if (!retained)
            m_tree.remove(previous.asElement().element);
    }
    if (property == m_props.Content)
        contentChanged(owner);
}
void Framework::contentChanged(ElementId owner)
{
    if (!m_tree.contains(owner))
        return;
    const auto active = contentObject(owner);
    ElementId visualOwner = owner;
    if (m_templateRoots.contains(owner))
    {
        const auto it = m_contentPresenters.find(owner);
        visualOwner = it == m_contentPresenters.end() || !m_tree.contains(it->second)
                          ? std::numeric_limits<ElementId>::max()
                          : it->second;
    }
    for (auto rank : kPropertySources)
    {
        const auto value = m_tree.propertyAt(owner, *m_props.Content, rank);
        if (value.isElement() && m_tree.parent(value.asElement().element) == owner)
            m_tree.setVisualParent(value.asElement().element, value.asElement().element == active
                                                                  ? visualOwner
                                                                  : std::numeric_limits<ElementId>::max());
    }
}
} // namespace velora
