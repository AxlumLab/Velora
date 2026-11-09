#include "velora/runtime/FrameworkInternal.hpp"

namespace velora
{
// ---- Toggle and range controls ------------------------------------------------------
// CheckBox, RadioButton and ToggleSwitch hold their state in IsChecked/IsOn; Slider and
// ProgressBar in Value within [Minimum, Maximum]. Framework::set routes changes of those
// properties here, so programmatic, XAML and user changes share coercion and events.

namespace
{
// Slider geometry: the thumb's centre travels between these insets of the element.
constexpr float kThumbRadius = 10;
} // namespace

bool Framework::pressable(std::optional<ElementKind> kind)
{
    return kind && isPressable(*kind);
}

bool Framework::interactive(std::optional<ElementKind> kind)
{
    return kind && isInteractive(*kind);
}

// User activation: CheckBox flips, RadioButton becomes checked (it cannot be unchecked by a
// click), ToggleSwitch flips.
void Framework::toggle(ElementId element)
{
    const auto kind = m_tree.kind(element);
    if (kind == ElementKind::kCheckBox)
        set(element, m_props.IsChecked,
            PropertyValue(!m_tree.property(element, *m_props.IsChecked).asBool()));
    else if (kind == ElementKind::kRadioButton)
        set(element, m_props.IsChecked, PropertyValue(true));
    else if (kind == ElementKind::kToggleSwitch)
        set(element, m_props.IsOn, PropertyValue(!m_tree.property(element, *m_props.IsOn).asBool()));
}

// RangeBase coercion: Maximum is at least Minimum, and Value lies within them.
PropertyValue Framework::coerceRange(ElementId element, const PropertyDescriptor* property,
                                     PropertyValue value) const
{
    const auto kind = m_tree.kind(element);
    if ((kind != ElementKind::kSlider && kind != ElementKind::kProgressBar) || !value.isDouble() ||
        std::isnan(value.asDouble()))
        return value;
    const double minimum = number(element, m_props.Minimum);
    if (property == m_props.Value)
        return PropertyValue(
            std::clamp(value.asDouble(), minimum, std::max(minimum, number(element, m_props.Maximum))));
    if (property == m_props.Maximum)
        return PropertyValue(std::max(value.asDouble(), minimum));
    return value;
}

void Framework::controlValueChanged(ElementId element, const PropertyDescriptor* property)
{
    const auto kind = m_tree.kind(element);
    // TwoWay {Binding} sources follow user and programmatic changes alike.
    if (property == m_props.IsChecked || property == m_props.IsOn || property == m_props.Value)
        m_bindings->writeBack(element, *property);
    if (property == m_props.IsChecked &&
        (kind == ElementKind::kCheckBox || kind == ElementKind::kRadioButton))
    {
        const bool checked = m_tree.property(element, *m_props.IsChecked).asBool();
        if (checked && kind == ElementKind::kRadioButton)
        {
            // One checked RadioButton per group: the named group within the page, or the
            // unnamed buttons sharing this parent.
            const std::string group(text(element, m_props.GroupName));
            const ElementId parent = m_tree.parent(element);
            std::vector<ElementId> others;
            const auto visit = [&](auto&& self, ElementId e) -> void
            {
                if (e != element && m_tree.is(e, ElementKind::kRadioButton) &&
                    text(e, m_props.GroupName) == group && (!group.empty() || m_tree.parent(e) == parent) &&
                    m_tree.property(e, *m_props.IsChecked).asBool())
                    others.push_back(e);
                for (const ElementId child : m_tree.children(e))
                    self(self, child);
            };
            if (group.empty())
            {
                for (const ElementId sibling : parent ? m_tree.children(parent) : m_tree.roots())
                    visit(visit, sibling);
            }
            else
            {
                ElementId scope = pageOwner(element);
                if (!scope)
                    for (scope = element; m_tree.parent(scope);)
                        scope = m_tree.parent(scope);
                visit(visit, scope);
            }
            for (const ElementId other : others)
                if (m_tree.contains(other))
                    set(other, m_props.IsChecked, PropertyValue(false));
        }
        if (m_tree.contains(element))
            m_tree.dispatch(element, (checked ? m_events.Checked : m_events.Unchecked)->id);
        return;
    }
    if (property == m_props.IsOn && kind == ElementKind::kToggleSwitch)
    {
        m_tree.dispatch(element, m_events.Toggled->id);
        return;
    }
    if (kind != ElementKind::kSlider && kind != ElementKind::kProgressBar)
        return;
    if (property != m_props.Value)
    {
        // A new range may move Maximum (above Minimum) and Value (into the range); each
        // re-coerced property keeps its source, and a moved Value raises ValueChanged.
        for (const auto* dependent : {m_props.Maximum, m_props.Value})
        {
            const PropertyValue current = m_tree.property(element, *dependent);
            if (const PropertyValue coerced = coerceRange(element, dependent, current); coerced != current)
                set(element, dependent, coerced,
                    m_tree.propertySource(element, *dependent).value_or(PropertySource::kLocal));
        }
        return;
    }
    if (kind == ElementKind::kSlider)
        m_tree.dispatch(element, m_events.ValueChanged->id);
}

// Pointer position to value: the thumb centre spans the width minus its radius at each end;
// the result snaps to StepFrequency from Minimum.
void Framework::sliderTo(ElementId slider, float x)
{
    if (!m_tree.contains(slider))
    {
        m_sliderDrag = 0;
        return;
    }
    const Box box = bounds(slider);
    const float span = std::max(1.0f, box.width - 2 * kThumbRadius);
    const double fraction = std::clamp((x - box.x - kThumbRadius) / span, 0.0f, 1.0f);
    const double minimum = number(slider, m_props.Minimum), maximum = number(slider, m_props.Maximum);
    double value = minimum + fraction * (maximum - minimum);
    if (const double step = number(slider, m_props.StepFrequency); step > 0)
        value = minimum + std::round((value - minimum) / step) * step;
    set(slider, m_props.Value, PropertyValue(value));
}

// Keyboard on a focused Slider (arrows step by StepFrequency, PageUp/PageDown by a tenth of
// the range, Home/End to the ends) or toggle control (Enter activates it). False when the
// focused element does not take the key.
bool Framework::controlKey(Key key)
{
    const ElementId focused = m_tree.focused();
    if (!focused || !m_tree.property(focused, *m_props.IsEnabled).asBool())
        return false;
    const auto kind = m_tree.kind(focused);
    if (kind == ElementKind::kButton && key == Key::kEnter)
    {
        m_tree.dispatch(focused, m_events.Click->id); // keyboard activation, as a click
        return true;
    }
    if (pressable(kind) && kind != ElementKind::kButton)
    {
        if (key != Key::kEnter)
            return false;
        toggle(focused);
        if (kind != ElementKind::kToggleSwitch && m_tree.contains(focused))
            m_tree.dispatch(focused, m_events.Click->id);
        return true;
    }
    if (kind != ElementKind::kSlider)
        return false;
    const double minimum = number(focused, m_props.Minimum), maximum = number(focused, m_props.Maximum);
    const double step = std::max(0.0, number(focused, m_props.StepFrequency));
    const double large = (maximum - minimum) / 10, value = number(focused, m_props.Value);
    double next = value;
    switch (key)
    {
    case Key::kLeft:
    case Key::kDown:
        next = value - step;
        break;
    case Key::kRight:
    case Key::kUp:
        next = value + step;
        break;
    case Key::kPageDown:
        next = value - large;
        break;
    case Key::kPageUp:
        next = value + large;
        break;
    case Key::kHome:
        next = minimum;
        break;
    case Key::kEnd:
        next = maximum;
        break;
    default:
        return false;
    }
    set(focused, m_props.Value, PropertyValue(next));
    return true;
}
} // namespace velora
