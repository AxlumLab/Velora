#include "velora/runtime/FrameworkInternal.hpp"
#include <utility>

#include <velora/controls/InputContext.hpp>
#include <velora/globals.hpp>

namespace velora
{
// ---- Input --------------------------------------------------------------------------

void Framework::focus(ElementId element)
{
    const ElementId previous = m_tree.focused();
    if (previous == element)
        return;
    if (previous && m_tree.is(previous, ElementKind::kTextBox) &&
        !text(previous, m_props.PreeditText).empty())
    {
        // Leaving a TextBox completes its composition, as the platform IME does on focus loss.
        Edit edit = editState(previous);
        const std::string committed(text(previous, m_props.PreeditText));
        const auto from = std::min(edit.caret, edit.anchor), to = std::max(edit.caret, edit.anchor);
        edit.text.replace(from, to - from, committed);
        edit.caret = edit.anchor = from + committed.size();
        set(previous, m_props.PreeditText, PropertyValue(std::string()));
        applyEdit(previous, edit, true);
    }
    if (previous)
        set(previous, m_props.IsFocused, PropertyValue(false));
    m_tree.focus(element);
    if (previous && m_tree.contains(previous))
        raiseRouted(previous, m_events.LostFocus);
    if (element)
    {
        set(element, m_props.IsFocused, PropertyValue(true));
        raiseRouted(element, m_events.GotFocus);
    }
}

u64 Framework::addHandler(ElementId element, const EventDescriptor* event, ElementTree::RoutedHandler handler,
                          bool handledEventsToo)
{
    if (!event || !isOwnerThread())
        return 0;
    return m_tree.addRoutedHandler(element, event->id, std::move(handler), handledEventsToo);
}

bool Framework::removeHandler(ElementId element, u64 token)
{
    return isOwnerThread() && m_tree.removeHandler(element, token);
}

EventSubscription Framework::subscribeEvent(ElementId element, const EventDescriptor* event,
                                            ElementTree::RoutedHandler handler, bool handledEventsToo)
{
    const u64 token = addHandler(element, event, std::move(handler), handledEventsToo);
    return token ? EventSubscription(*this, element, token) : EventSubscription{};
}

bool Framework::raiseRouted(ElementId target, const EventDescriptor* event, RoutedEventArgs* input)
{
    if (!target || !m_tree.contains(target))
        return false;
    if (input)
        for (ElementId current = target; current; current = m_tree.parent(current))
            if (!m_tree.property(current, *m_props.IsEnabled).asBool() ||
                !m_tree.property(current, *m_props.Visibility).asBool())
                return true;
    // Class handling: the nearest enabled interactive control on the route consumes pointer
    // presses and releases (UWP ButtonBase/TextBox/Slider OnPointerPressed), before its own
    // instance handlers, so they and its ancestors see them only with handledEventsToo.
    ElementId handledAt = 0;
    if (event == m_events.PointerPressed || event == m_events.PointerReleased)
        for (ElementId e = target; e && !handledAt; e = m_tree.parent(e))
            if (interactive(m_tree.kind(e)) && m_tree.property(e, *m_props.IsEnabled).asBool())
                handledAt = e;
    const auto alive = m_lifetime;
    RoutedEventArgs empty;
    auto& args = input ? *input : empty;
    args.originalSource = target;
    bool overridden = false;
    const auto classHandler = [&](ElementId sender, RoutedEventArgs& routed)
    {
        auto* pointer = dynamic_cast<PointerEventArgs*>(&routed);
        if (pointer)
        {
            const Box box = bounds(sender);
            pointer->x = pointer->windowX - box.x;
            pointer->y = pointer->windowY - box.y;
        }
        if (routed.handled)
            return;
        const auto object = m_tree.object(sender);
        if (!object)
            return;
        auto& element = static_cast<UIElement&>(*object);
        InputContext context(*this, sender);
        if (pointer)
        {
            if (event == m_events.PointerPressed)
                element.onPointerPressed(context, *pointer);
            else if (event == m_events.PointerMoved)
                element.onPointerMoved(context, *pointer);
            else if (event == m_events.PointerReleased)
                element.onPointerReleased(context, *pointer);
        }
        else if (auto* key = dynamic_cast<KeyEventArgs*>(&routed))
            element.onKeyDown(context, *key);
        overridden = overridden || routed.handled;
    };
    const bool handled = m_tree.raise(target, event->id, args, handledAt, classHandler);
    // Existing built-in class handling marks pointer events Handled for instance subscribers;
    // its implementation still runs below. An override may consume input before that fallback.
    return !*alive || overridden || (!handledAt && handled) || !m_tree.contains(target);
}

void Framework::pointerMove(float x, float y)
{
    PointerEventArgs args;
    args.windowX = x;
    args.windowY = y;
    if (raiseRouted(m_tree.pointerCapture() ? m_tree.pointerCapture() : hitTest(x, y), m_events.PointerMoved,
                    &args))
        return;
    scrollBarMove(x, y);
    if (m_scrollDrag.viewer)
        return;
    if (m_sliderDrag)
    {
        sliderTo(m_sliderDrag, x);
        return;
    }
    ElementId target = hitTest(x, y);
    while (target && !interactive(m_tree.kind(target)))
        target = m_tree.parent(target);
    if (target == m_hovered)
        return;
    if (m_hovered && m_tree.contains(m_hovered))
        set(m_hovered, m_props.IsPointerOver, PropertyValue(false));
    m_hovered = target;
    if (target)
        set(target, m_props.IsPointerOver, PropertyValue(true));
}

void Framework::pointerDown(float x, float y)
{
    m_dismissedPopupPress = false;
    if (m_popup && !hit(m_popup->root, x, y, 0, 0))
    {
        m_dismissedPopupPress = true;
        closePopup();
        return;
    }
    if (scrollBarDown(x, y))
        return;
    PointerEventArgs args;
    args.windowX = x;
    args.windowY = y;
    if (raiseRouted(m_tree.pointerCapture() ? m_tree.pointerCapture() : hitTest(x, y),
                    m_events.PointerPressed, &args))
        return;
    ElementId target = hitTest(x, y);
    while (target && !interactive(m_tree.kind(target)))
        target = m_tree.parent(target);
    if (!target || !m_tree.property(target, *m_props.IsEnabled).asBool())
    {
        // A press on a ScrollViewer's content focuses the viewer, so the keyboard scrolls it.
        ElementId viewer = hitTest(x, y);
        while (viewer && !m_tree.is(viewer, ElementKind::kScrollViewer))
            viewer = m_tree.parent(viewer);
        focus(viewer);
        return;
    }
    if (m_tree.is(target, ElementKind::kTextBox))
    {
        focus(target);
        if (text(target, m_props.PreeditText).empty())
        {
            Edit edit = editState(target);
            edit.caret = edit.anchor = caretAt(target, x);
            applyEdit(target, edit, false);
        }
        return;
    }
    if (m_tree.is(target, ElementKind::kSlider))
    {
        // The thumb jumps to the pointer and follows it until release; the Slider takes focus
        // so the arrow keys step it.
        focus(target);
        m_sliderDrag = target;
        m_tree.capturePointer(target);
        sliderTo(target, x);
        return;
    }
    focus(m_tree.is(target, ElementKind::kButton) ? 0 : target);
    m_pressed = target;
    set(target, m_props.IsPressed, PropertyValue(true));
    m_tree.capturePointer(target);
}

void Framework::pointerUp(float x, float y)
{
    if (std::exchange(m_dismissedPopupPress, false))
        return;
    const auto alive = m_lifetime;
    PointerEventArgs args;
    args.windowX = x;
    args.windowY = y;
    const bool handled = raiseRouted(m_tree.pointerCapture() ? m_tree.pointerCapture() : hitTest(x, y),
                                     m_events.PointerReleased, &args);
    if (!*alive)
        return;
    if (m_scrollDrag.viewer)
    {
        m_scrollDrag = {};
        m_tree.releasePointer();
        scrollBarMove(x, y); // collapse the bar if the pointer left it while dragging
        return;
    }
    if (m_sliderDrag)
    {
        m_sliderDrag = 0;
        m_tree.releasePointer();
        return;
    }
    if (!m_pressed)
        return;
    const ElementId button = m_pressed;
    m_pressed = 0;
    m_tree.releasePointer();
    if (!m_tree.contains(button))
        return;
    set(button, m_props.IsPressed, PropertyValue(false));
    if (handled)
        return;
    if (!bounds(button).contains(x, y))
        return;
    // Toggle controls change state first (raising Checked/Unchecked/Toggled), then Click, as
    // ToggleButton.OnClick does. ToggleSwitch is not a ButtonBase and has no Click.
    const auto kind = m_tree.kind(button);
    if (kind != ElementKind::kButton)
        toggle(button);
    if (kind != ElementKind::kToggleSwitch && m_tree.contains(button))
        m_tree.dispatch(button, m_events.Click->id);
}

Framework::Edit Framework::editState(ElementId box) const
{
    Edit edit{std::string(text(box, m_props.Text))};
    const auto offset = [&](const PropertyDescriptor* d)
    {
        return clampBoundary(edit.text,
                             static_cast<std::size_t>(std::max<i64>(0, m_tree.property(box, *d).asInt())));
    };
    edit.caret = offset(m_props.CaretPosition);
    edit.anchor = offset(m_props.SelectionAnchor);
    return edit;
}

void Framework::applyEdit(ElementId box, const Edit& edit, bool textChanged)
{
    set(box, m_props.CaretPosition, PropertyValue(static_cast<i64>(edit.caret)));
    set(box, m_props.SelectionAnchor, PropertyValue(static_cast<i64>(edit.anchor)));
    if (!textChanged || edit.text == text(box, m_props.Text))
        return;
    set(box, m_props.Text, PropertyValue(edit.text));
    m_bindings->writeBack(box, *m_props.Text);
    m_tree.dispatch(box, m_events.TextChanged->id);
}

std::size_t Framework::caretAt(ElementId box, float x) const
{
    const std::string value(text(box, m_props.Text));
    const auto font = static_cast<float>(number(box, m_props.FontSize));
    const float local = x - bounds(box).x -
                        static_cast<float>(thickness(box, m_props.Padding).left +
                                           thickness(box, m_props.BorderThickness).left);
    std::size_t best = 0;
    float bestDistance = std::abs(local);
    for (std::size_t at = 0; at < value.size();)
    {
        at = nextBoundary(value, at);
        const float distance = std::abs(m_measure(std::string_view(value).substr(0, at), font) - local);
        if (distance < bestDistance)
        {
            best = at;
            bestDistance = distance;
        }
    }
    return best;
}

void Framework::textInput(std::string_view utf8)
{
    const ElementId box = m_tree.focused();
    if (!box || !m_tree.is(box, ElementKind::kTextBox))
        return;
    set(box, m_props.PreeditText, PropertyValue(std::string()));
    Edit edit = editState(box);
    const auto from = std::min(edit.caret, edit.anchor), to = std::max(edit.caret, edit.anchor);
    edit.text.replace(from, to - from, utf8);
    edit.caret = edit.anchor = from + utf8.size();
    applyEdit(box, edit, true);
}

void Framework::textEditing(std::string_view utf8, std::size_t cursor)
{
    const ElementId box = m_tree.focused();
    if (!box || !m_tree.is(box, ElementKind::kTextBox))
        return;
    set(box, m_props.PreeditText, PropertyValue(std::string(utf8)));
    set(box, m_props.PreeditCursor, PropertyValue(static_cast<i64>(cursor)));
}

bool Framework::moveFocus(bool forward)
{
    const auto tabStop = [&](ElementId e)
    {
        const auto* element = m_tree.uiElement(e);
        if (!element || !element->isFocusable())
            return false;
        if (!m_tree.property(e, *m_props.IsTabStop).asBool())
            return false;
        for (ElementId a = e; a; a = m_tree.parent(a))
            if (!m_tree.property(a, *m_props.Visibility).asBool() ||
                !m_tree.property(a, *m_props.IsEnabled).asBool())
                return false;
        return true;
    };
    std::vector<ElementId> stops;
    const auto walk = [&](auto&& self, ElementId e) -> void
    {
        if (tabStop(e))
            stops.push_back(e);
        for (const ElementId child : m_tree.children(e))
            self(self, child);
    };
    for (const ElementId root : m_tree.visualRoots())
        if (isMounted(root) && (!m_popup || root == m_popup->root))
            walk(walk, root);
    if (stops.empty())
        return false;
    // TabIndex orders the stops; equal indices keep tree order (UWP).
    std::ranges::stable_sort(stops, {},
                             [&](ElementId e) { return m_tree.property(e, *m_props.TabIndex).asInt(); });
    const auto at = std::ranges::find(stops, m_tree.focused());
    usize next = 0;
    if (at == stops.end())
        next = forward ? 0 : stops.size() - 1;
    else
    {
        const auto index = static_cast<usize>(at - stops.begin());
        next = forward ? (index + 1) % stops.size() : (index + stops.size() - 1) % stops.size();
    }
    focus(stops[next]);
    return true;
}

void Framework::key(Key key, bool shift, bool ctrl, bool alt)
{
    if (m_popup && key == Key::kEscape)
    {
        closePopup();
        return;
    }
    KeyEventArgs args;
    args.key = key;
    args.shift = shift;
    args.ctrl = ctrl;
    args.alt = alt;
    if (raiseRouted(m_tree.focused(), m_events.KeyDown, &args))
        return;
    if (key == Key::kTab)
    {
        moveFocus(!shift);
        return;
    }
    const ElementId box = m_tree.focused();
    // A TextBox handles its editing keys; scrolling keys bubble to the surrounding ScrollViewer.
    const bool scrolling =
        key == Key::kUp || key == Key::kDown || key == Key::kPageUp || key == Key::kPageDown;
    if (!box || !m_tree.is(box, ElementKind::kTextBox) || scrolling)
    {
        if (!controlKey(key))
            scrollKey(key);
        return;
    }
    if (!text(box, m_props.PreeditText).empty())
        return;
    Edit edit = editState(box);
    const auto from = std::min(edit.caret, edit.anchor), to = std::max(edit.caret, edit.anchor);
    const auto move = [&](std::size_t at)
    {
        edit.caret = at;
        if (!shift)
            edit.anchor = at;
    };
    bool changed = false;
    switch (key)
    {
    case Key::kLeft:
        move(from != to && !shift ? from : previousBoundary(edit.text, edit.caret));
        break;
    case Key::kRight:
        move(from != to && !shift ? to : nextBoundary(edit.text, edit.caret));
        break;
    case Key::kHome:
        move(0);
        break;
    case Key::kEnd:
        move(edit.text.size());
        break;
    case Key::kBackspace:
    case Key::kDelete:
    {
        std::size_t first = from, last = to;
        if (first == last)
        {
            if (key == Key::kBackspace)
                first = previousBoundary(edit.text, first);
            else
                last = nextBoundary(edit.text, last);
        }
        if (first == last)
            break;
        edit.text.erase(first, last - first);
        edit.caret = edit.anchor = first;
        changed = true;
        break;
    }
    case Key::kA:
        if (ctrl)
        {
            edit.anchor = 0;
            edit.caret = edit.text.size();
        }
        break;
    case Key::kEnter:
    case Key::kUp:
    case Key::kDown:
    case Key::kPageUp:
    case Key::kPageDown:
        break;
    }
    applyEdit(box, edit, changed);
}

Box Framework::caretBounds() const
{
    const ElementId box = m_tree.focused();
    if (!box || !m_tree.is(box, ElementKind::kTextBox))
        return {};
    const Edit edit = editState(box);
    const auto font = static_cast<float>(number(box, m_props.FontSize));
    const std::string_view preedit = text(box, m_props.PreeditText);
    const auto cursor = static_cast<std::size_t>(m_tree.property(box, *m_props.PreeditCursor).asInt());
    const std::string before =
        edit.text.substr(0, edit.caret) + std::string(preedit.substr(0, codePointPrefix(preedit, cursor)));
    const Box b = bounds(box);
    const float x = b.x +
                    static_cast<float>(thickness(box, m_props.Padding).left +
                                       thickness(box, m_props.BorderThickness).left) +
                    m_measure(before, font);
    return {x, b.y + (b.height - lineHeight(font)) / 2, 1, lineHeight(font)};
}
} // namespace velora
