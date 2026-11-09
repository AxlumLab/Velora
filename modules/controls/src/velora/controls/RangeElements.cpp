#include <algorithm>
#include <cmath>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/ElementFactory.hpp>

namespace velora
{

const PropertyDescriptor& RangeBaseElement::minimumProperty()
{
    return *elementProperties().props.Minimum;
}

const PropertyDescriptor& RangeBaseElement::maximumProperty()
{
    return *elementProperties().props.Maximum;
}

const PropertyDescriptor& RangeBaseElement::valueProperty()
{
    return *elementProperties().props.Value;
}

f64 RangeBaseElement::minimum() const
{
    return getValue(minimumProperty()).asDouble();
}

f64 RangeBaseElement::maximum() const
{
    return getValue(maximumProperty()).asDouble();
}

f64 RangeBaseElement::value() const
{
    return getValue(valueProperty()).asDouble();
}

PropertySetResult RangeBaseElement::setMinimum(f64 value, PropertySource source)
{
    return setValue(minimumProperty(), PropertyValue(value), source);
}

PropertySetResult RangeBaseElement::setMaximum(f64 value, PropertySource source)
{
    return setValue(maximumProperty(), PropertyValue(value), source);
}

PropertySetResult RangeBaseElement::setValue(f64 value, PropertySource source)
{
    return setValue(valueProperty(), PropertyValue(value), source);
}

bool RangeBaseElement::validatePropertyValue(const PropertyDescriptor& property,
                                             const PropertyValue& value) const
{
    const auto& p = elementProperties().props;
    const bool range =
        property.id == p.Minimum->id || property.id == p.Maximum->id || property.id == p.Value->id;
    const bool step = kind() == ElementKind::kSlider && property.id == p.StepFrequency->id;
    return (!range && !step) ||
           (value.isDouble() && std::isfinite(value.asDouble()) && (!step || value.asDouble() >= 0));
}

void RangeBaseElement::normalizePropertyValues(const PropertyDescriptor& property)
{
    if (property.id != minimumProperty().id && property.id != maximumProperty().id &&
        property.id != valueProperty().id)
        return;
    const f64 low = minimum();
    const f64 high = std::max(low, maximum());
    normalizeValue(maximumProperty(), PropertyValue(high));
    normalizeValue(valueProperty(), PropertyValue(std::clamp(value(), low, high)));
}

const PropertyDescriptor& SliderElement::stepFrequencyProperty()
{
    return *elementProperties().props.StepFrequency;
}

f64 SliderElement::stepFrequency() const
{
    return getValue(stepFrequencyProperty()).asDouble();
}

PropertySetResult SliderElement::setStepFrequency(f64 value, PropertySource source)
{
    return setValue(stepFrequencyProperty(), PropertyValue(value), source);
}

} // namespace velora
