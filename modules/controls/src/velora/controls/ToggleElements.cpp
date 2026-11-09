#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/ElementFactory.hpp>

namespace velora
{

const PropertyDescriptor& ToggleButtonElement::isCheckedProperty()
{
    return *elementProperties().props.IsChecked;
}

bool ToggleButtonElement::isChecked() const
{
    return getValue(isCheckedProperty()).asBool();
}

PropertySetResult ToggleButtonElement::setIsChecked(bool value, PropertySource source)
{
    return setValue(isCheckedProperty(), PropertyValue(value), source);
}

const PropertyDescriptor& ToggleSwitchElement::isOnProperty()
{
    return *elementProperties().props.IsOn;
}

bool ToggleSwitchElement::isOn() const
{
    return getValue(isOnProperty()).asBool();
}

PropertySetResult ToggleSwitchElement::setIsOn(bool value, PropertySource source)
{
    return setValue(isOnProperty(), PropertyValue(value), source);
}

} // namespace velora
