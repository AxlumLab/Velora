#include "FrameworkInternal.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace velora
{
ElementId Framework::popupRoot() const noexcept
{
    return m_popup ? m_popup->root : 0;
}
bool Framework::popupAnchorAvailable(ElementId element) const
{
    if (!m_tree.contains(element))
        return false;
    for (auto current = element; current; current = m_tree.parent(current))
    {
        if (!m_tree.property(current, *m_props.Visibility).asBool() ||
            !m_tree.property(current, *m_props.IsEnabled).asBool())
            return false;
        if (!m_tree.parent(current) && !isMounted(current))
            return false;
    }
    return true;
}
bool Framework::openPopup(ElementId anchor, ElementId root, std::function<void()> closed,
                          ElementId passThrough)
{
    if (!isOwnerThread() || m_destroying || !popupAnchorAvailable(anchor) || !m_tree.contains(root) ||
        m_tree.parent(root) || root == anchor || isWithin(anchor, root) ||
        (passThrough && (!m_tree.contains(passThrough) || !isWithin(anchor, passThrough))))
        return false;
    const auto alive = m_lifetime;
    closePopup(false);
    if (!*alive || m_popup || !popupAnchorAvailable(anchor) || !m_tree.contains(root))
        return false;
    const auto restore = isWithin(focused(), anchor) ? anchor : focused();
    m_popup = PopupState{anchor, root, restore, std::move(closed), passThrough};
    m_tree.onRemoved(root,
                     [this, root]
                     {
                         if (popupRoot() == root)
                             closePopup();
                     });
    m_tree.releasePointer();
    setMounted(root, true);
    set(root, m_visual.zIndex, i64{std::numeric_limits<int>::max()});
    if (!*alive || popupRoot() != root)
        return false;
    layoutPopup();
    if (!*alive || popupRoot() != root)
        return false;
    focus(0);
    if (!*alive || popupRoot() != root)
        return false;
    if (!moveFocus(true))
        focus(root);
    return *alive && popupRoot() == root;
}
void Framework::closePopup(bool restoreFocus)
{
    if (!isOwnerThread() || !m_popup)
        return;
    auto state = std::move(*m_popup);
    m_popup.reset();
    const auto alive = m_lifetime;
    const bool heldFocus = !focused() || isWithin(focused(), state.root);
    if (m_tree.contains(state.root))
        m_tree.remove(state.root);
    if (!*alive)
        return;
    if (restoreFocus && heldFocus && !m_popup)
    {
        const auto target = popupAnchorAvailable(state.restore)  ? state.restore
                            : popupAnchorAvailable(state.anchor) ? state.anchor
                                                                 : 0;
        focus(target);
    }
    if (state.closed)
    {
        try
        {
            state.closed();
        }
        catch (...)
        {
        } // same isolation as routed token handlers; also safe during teardown
    }
}
void Framework::layoutPopup()
{
    if (!m_popup || m_width < 0 || m_height < 0)
        return;
    const auto state = *m_popup;
    if (!popupAnchorAvailable(state.anchor) || !m_tree.contains(state.root))
    {
        closePopup(false);
        return;
    }
    const auto anchor = bounds(state.anchor);
    const auto alive = m_lifetime;
    const auto size = measure(state.root, {m_width, m_height});
    if (!*alive || !m_popup || m_popup->root != state.root)
        return;
    const auto width = std::clamp(size.width, 0.0f, m_width);
    const auto height = std::clamp(size.height, 0.0f, m_height);
    const auto x = std::clamp(anchor.x, 0.0f, m_width - width);
    auto y = anchor.y + anchor.height + 4;
    if (y + height > m_height)
        y = anchor.y - height - 4;
    y = std::clamp(y, 0.0f, m_height - height);
    arrange(state.root, {x, y, width, height});
}
} // namespace velora
