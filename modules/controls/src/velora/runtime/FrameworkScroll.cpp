#include "velora/runtime/FrameworkInternal.hpp"

namespace velora
{
// ---- Scrolling ----------------------------------------------------------------------

namespace
{
// Scroll bar metrics (DIPs). Collapsed bars are WinUI's panning indicators; an expanded bar is
// the full bar with arrows, shown while the pointer is over it.
constexpr float kBarSize = 12, kIndicator = 3, kInset = 2, kMinThumb = 16;
// One arrow press or arrow key: a 16-DIP line; one wheel notch is three lines (XamlHost).
constexpr double kLineDelta = 16;
} // namespace

Framework::Viewport Framework::viewport(ElementId list) const
{
    const auto it = m_lists.find(list);
    return it == m_lists.end() ? Viewport{} : it->second.current;
}

void Framework::scrollTo(ElementId list, double offset)
{
    const auto it = m_lists.find(list);
    if (it == m_lists.end())
        return;
    const auto& v = it->second.current;
    const double extent = v.itemHeight * static_cast<double>(v.count);
    set(list, m_props.ScrollOffset, PropertyValue(std::clamp(offset, 0.0, std::max(0.0, extent - v.height))));
}

bool Framework::scrollable(ElementId e, const PropertyDescriptor* d) const
{
    return text(e, d) != "Disabled";
}

bool Framework::changeView(ElementId scroller, std::optional<double> horizontal,
                           std::optional<double> vertical)
{
    if (!m_tree.contains(scroller) || !m_tree.is(scroller, ElementKind::kScrollViewer))
        return false;
    if ((horizontal && !std::isfinite(*horizontal)) || (vertical && !std::isfinite(*vertical)))
        return false;
    const auto apply = [&](std::optional<double> value, const PropertyDescriptor* offset,
                           const PropertyDescriptor* extent, const PropertyDescriptor* viewport,
                           const PropertyDescriptor* visibility)
    {
        if (!value || !scrollable(scroller, visibility))
            return;
        // Before the first layout the range is unknown; arrange clamps the request then.
        const double range = number(scroller, viewport) > 0
                                 ? std::max(0.0, number(scroller, extent) - number(scroller, viewport))
                                 : std::numeric_limits<double>::infinity();
        set(scroller, offset, PropertyValue(std::clamp(*value, 0.0, range)));
    };
    apply(horizontal, m_props.HorizontalOffset, m_props.ExtentWidth, m_props.ViewportWidth,
          m_props.HorizontalScrollBarVisibility);
    apply(vertical, m_props.VerticalOffset, m_props.ExtentHeight, m_props.ViewportHeight,
          m_props.VerticalScrollBarVisibility);
    return true;
}

// Moves one axis of a ScrollViewer by `delta`; false when it cannot move that way (disabled
// axis, or already at that end), so the caller can chain to an outer scroller.
bool Framework::scrollStep(ElementId viewer, bool vertical, double delta)
{
    const auto* visibility =
        vertical ? m_props.VerticalScrollBarVisibility : m_props.HorizontalScrollBarVisibility;
    if (delta == 0 || !scrollable(viewer, visibility))
        return false;
    const double current = number(viewer, vertical ? m_props.VerticalOffset : m_props.HorizontalOffset);
    const double range =
        std::max(0.0, number(viewer, vertical ? m_props.ExtentHeight : m_props.ExtentWidth) -
                          number(viewer, vertical ? m_props.ViewportHeight : m_props.ViewportWidth));
    if (delta > 0 ? current >= range : current <= 0)
        return false;
    changeView(viewer, vertical ? std::nullopt : std::optional(current + delta),
               vertical ? std::optional(current + delta) : std::nullopt);
    return true;
}

void Framework::wheel(float x, float y, float delta)
{
    const auto alive = m_lifetime;
    if (m_popup && m_popup->passive)
        closePopup(false);
    if (!*alive)
        return;
    if (m_popup && !hit(m_popup->root, x, y, 0, 0))
        return;
    // The innermost scroller that can still move takes the wheel; at its end the wheel chains
    // to the next one out, as in WinUI. A viewer that cannot scroll vertically scrolls sideways.
    for (ElementId e = hitTest(x, y); e; e = m_tree.parent(e))
    {
        if (m_lists.contains(e))
        {
            const auto v = viewport(e);
            const double range = std::max(0.0, v.itemHeight * static_cast<double>(v.count) - v.height);
            if (delta > 0 ? v.offset < range : v.offset > 0)
                return scrollTo(e, v.offset + delta);
            continue;
        }
        if (!m_tree.is(e, ElementKind::kScrollViewer))
            continue;
        if (scrollStep(e, true, delta))
            return;
        if (!scrollable(e, m_props.VerticalScrollBarVisibility) && scrollStep(e, false, delta))
            return;
    }
}

// Keyboard scrolling for the ScrollViewer around the focused element (or the focused viewer):
// arrows move a line, PageUp/PageDown a viewport, Home/End to the ends. Keys bubble outwards
// while a viewer cannot move.
bool Framework::scrollKey(Key key)
{
    for (ElementId e = m_tree.focused(); e; e = m_tree.parent(e))
    {
        if (!m_tree.is(e, ElementKind::kScrollViewer))
            continue;
        const double page = number(e, m_props.ViewportHeight);
        const double extent = number(e, m_props.ExtentHeight);
        bool moved = false;
        switch (key)
        {
        case Key::kUp:
            moved = scrollStep(e, true, -kLineDelta);
            break;
        case Key::kDown:
            moved = scrollStep(e, true, kLineDelta);
            break;
        case Key::kLeft:
            moved = scrollStep(e, false, -kLineDelta);
            break;
        case Key::kRight:
            moved = scrollStep(e, false, kLineDelta);
            break;
        case Key::kPageUp:
            moved = scrollStep(e, true, -page);
            break;
        case Key::kPageDown:
            moved = scrollStep(e, true, page);
            break;
        case Key::kHome:
            moved = scrollStep(e, true, -extent);
            break;
        case Key::kEnd:
            moved = scrollStep(e, true, extent);
            break;
        default:
            return false;
        }
        if (moved)
            return true;
    }
    return false;
}

Framework::ScrollBarParts Framework::scrollBar(ElementId viewer, bool vertical) const
{
    ScrollBarParts parts;
    // Visible always shows a bar, Auto only while there is something to scroll.
    const auto hasBar = [&](bool v)
    {
        const auto shown =
            text(viewer, v ? m_props.VerticalScrollBarVisibility : m_props.HorizontalScrollBarVisibility);
        const double extent = number(viewer, v ? m_props.ExtentHeight : m_props.ExtentWidth);
        const double port = number(viewer, v ? m_props.ViewportHeight : m_props.ViewportWidth);
        return shown != "Disabled" && shown != "Hidden" && port > 0 && (shown != "Auto" || extent > port);
    };
    if (!hasBar(vertical))
        return parts;
    const double extent = number(viewer, vertical ? m_props.ExtentHeight : m_props.ExtentWidth);
    const double port = number(viewer, vertical ? m_props.ViewportHeight : m_props.ViewportWidth);
    parts.shown = true;
    parts.expanded = (m_tree.property(viewer, *m_props.ScrollBarExpanded).asInt() & (vertical ? 1 : 2)) != 0;
    const auto w = static_cast<float>(number(viewer, m_visual.width));
    const auto h = static_cast<float>(number(viewer, m_visual.height));
    // The other axis's bar leaves the corner free when both are shown.
    const bool other = hasBar(!vertical);
    const float length = (vertical ? h : w) - (other ? kBarSize : 0);
    parts.bar = vertical ? Box{w - kBarSize, 0, kBarSize, length} : Box{0, h - kBarSize, length, kBarSize};
    const float arrows = parts.expanded ? kBarSize : kInset;
    const float track = std::max(0.0f, length - 2 * arrows);
    if (parts.expanded)
    {
        parts.decrease =
            vertical ? Box{w - kBarSize, 0, kBarSize, kBarSize} : Box{0, h - kBarSize, kBarSize, kBarSize};
        parts.increase = vertical ? Box{w - kBarSize, length - kBarSize, kBarSize, kBarSize}
                                  : Box{length - kBarSize, h - kBarSize, kBarSize, kBarSize};
    }
    parts.track =
        vertical ? Box{w - kBarSize, arrows, kBarSize, track} : Box{arrows, h - kBarSize, track, kBarSize};
    const float thumb = std::clamp(static_cast<float>(track * port / std::max(extent, port)),
                                   std::min(kMinThumb, track), track);
    const double range = extent - port;
    const double offset = number(viewer, vertical ? m_props.VerticalOffset : m_props.HorizontalOffset);
    const float at = arrows + (range > 0 ? static_cast<float>((track - thumb) * offset / range) : 0);
    // Collapsed: a thin indicator along the edge; expanded: a 6-DIP thumb centred in the bar.
    const float thickness = parts.expanded ? 6 : kIndicator;
    const float across = parts.expanded ? (kBarSize - thickness) / 2 : kBarSize - kIndicator - kInset;
    parts.thumb = vertical ? Box{w - kBarSize + across, at, thickness, thumb}
                           : Box{at, h - kBarSize + across, thumb, thickness};
    return parts;
}

// Pointer press on an expanded scroll bar: arrows step a line, the track pages towards the
// pointer, the thumb starts a drag. True when a bar took the press.
bool Framework::scrollBarDown(float x, float y)
{
    for (ElementId e = hitTest(x, y); e; e = m_tree.parent(e))
    {
        if (!m_tree.is(e, ElementKind::kScrollViewer))
            continue;
        const Box origin = bounds(e);
        const float lx = x - origin.x, ly = y - origin.y;
        for (const bool vertical : {true, false})
        {
            const auto parts = scrollBar(e, vertical);
            if (!parts.expanded || !parts.bar.contains(lx, ly))
                continue;
            const float along = vertical ? ly : lx;
            const float thumbStart = vertical ? parts.thumb.y : parts.thumb.x;
            const float thumbLength = vertical ? parts.thumb.height : parts.thumb.width;
            const double page = number(e, vertical ? m_props.ViewportHeight : m_props.ViewportWidth);
            if (parts.decrease.contains(lx, ly))
                scrollStep(e, vertical, -kLineDelta);
            else if (parts.increase.contains(lx, ly))
                scrollStep(e, vertical, kLineDelta);
            else if (along < thumbStart)
                scrollStep(e, vertical, -page);
            else if (along >= thumbStart + thumbLength)
                scrollStep(e, vertical, page);
            else
            {
                m_scrollDrag = {e, vertical, along - thumbStart};
                m_tree.capturePointer(e);
            }
            return true;
        }
    }
    return false;
}

// Pointer movement: drags the pressed thumb, or expands the bar under the pointer (and
// collapses the one it left).
void Framework::scrollBarMove(float x, float y)
{
    if (const ElementId viewer = m_scrollDrag.viewer)
    {
        const bool vertical = m_scrollDrag.vertical;
        const auto parts = scrollBar(viewer, vertical);
        const Box origin = bounds(viewer);
        const float along = vertical ? y - origin.y : x - origin.x;
        const float trackStart = vertical ? parts.track.y : parts.track.x;
        const float free =
            (vertical ? parts.track.height - parts.thumb.height : parts.track.width - parts.thumb.width);
        const double range =
            std::max(0.0, number(viewer, vertical ? m_props.ExtentHeight : m_props.ExtentWidth) -
                              number(viewer, vertical ? m_props.ViewportHeight : m_props.ViewportWidth));
        const double offset = free > 0 ? (along - m_scrollDrag.grab - trackStart) / free * range : 0;
        changeView(viewer, vertical ? std::nullopt : std::optional(offset),
                   vertical ? std::optional(offset) : std::nullopt);
        return;
    }
    ElementId over = 0;
    i64 bars = 0;
    for (ElementId e = hitTest(x, y); e && !over; e = m_tree.parent(e))
    {
        if (!m_tree.is(e, ElementKind::kScrollViewer))
            continue;
        const Box origin = bounds(e);
        for (const bool vertical : {true, false})
            if (const auto parts = scrollBar(e, vertical);
                parts.shown && parts.bar.contains(x - origin.x, y - origin.y))
            {
                over = e;
                bars = vertical ? 1 : 2;
                break;
            }
    }
    if (m_scrollHover && m_scrollHover != over && m_tree.contains(m_scrollHover))
        set(m_scrollHover, m_props.ScrollBarExpanded, PropertyValue(i64{0}));
    m_scrollHover = over;
    if (over)
        set(over, m_props.ScrollBarExpanded, PropertyValue(bars));
}

// ViewChanged after layout, for every viewer whose offsets moved since the last report.
// Handlers may change or remove viewers, so the set is snapshotted first.
bool Framework::notifyViewChanged()
{
    std::vector<ElementId> changed;
    for (auto& [id, last] : m_viewOffsets)
    {
        const std::pair now{number(id, m_props.HorizontalOffset), number(id, m_props.VerticalOffset)};
        if (now != last)
        {
            last = now;
            changed.push_back(id);
        }
    }
    std::ranges::sort(changed);
    for (const ElementId id : changed)
        if (m_tree.contains(id))
            m_tree.dispatch(id, m_events.ViewChanged->id);
    return !changed.empty();
}

void Framework::onViewport(ElementId list, std::function<void()> callback)
{
    const auto it = m_lists.find(list);
    if (it == m_lists.end())
        return;
    it->second.callback = std::move(callback);
    it->second.notified = {-1, -1, -1, 0}; // the next update reports the current viewport
}
} // namespace velora
