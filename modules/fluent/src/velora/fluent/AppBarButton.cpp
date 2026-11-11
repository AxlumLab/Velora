// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Internal implementation of the system AppBar button behaviors; templates derive from
// WinUI 2.3 CommonStyles/AppBarButton and AppBarToggleButton resources.
#include <limits>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/AppBarButton.hpp>
namespace velora
{
namespace
{
const ReadOnlyPropertyKey& overflowKey()
{
    static PropertyRegistry registry;
    static const auto key = []
    {
        PropertyDescriptor property;
        property.name = "AppBarButton.IsInOverflow";
        property.defaultValue = false;
        property.flags = PropertyFlags::kPaint;
        return registry.defineReadOnly(std::move(property));
    }();
    return key;
}
} // namespace
const PropertyDescriptor& AppBarButton::isInOverflowProperty()
{
    return overflowKey().property();
}
bool AppBarButton::isInOverflow() const noexcept
{
    return getValue(isInOverflowProperty()).asBool();
}
void AppBarButton::setOverflowMode(bool value)
{
    setValue(overflowKey(), value);
}
void AppBarButton::onDetached() noexcept
{
    FlyoutButton::onDetached();
    try
    {
        setOverflowMode(false);
    }
    catch (...)
    {
    }
}
AppBarButton::AppBarButton() : FlyoutButton(Variant::kAppBarButton)
{
}
AppBarButton::AppBarButton(Variant variant) : FlyoutButton(variant)
{
}
AppBarToggleButton::AppBarToggleButton() : AppBarButton(Variant::kAppBarToggleButton)
{
}
#define VELORA_APPBAR(type)                                                                                  \
    const XamlTypeDeclaration& type::declaration()                                                           \
    {                                                                                                        \
        return typeDeclaration("Velora.Fluent." #type);                                                      \
    }                                                                                                        \
    const PropertyDescriptor& type::labelProperty()                                                          \
    {                                                                                                        \
        return propertyFor("Velora.Fluent." #type, "Label");                                                 \
    }                                                                                                        \
    std::string type::registerType(Framework& ui)                                                            \
    {                                                                                                        \
        return registerControl(ui, declaration(), [] { return createElement<type>(); });                     \
    }
VELORA_APPBAR(AppBarButton)
VELORA_APPBAR(AppBarToggleButton)
#undef VELORA_APPBAR
const PropertyDescriptor& AppBarButton::dynamicOverflowOrderProperty()
{
    return propertyFor("Velora.Fluent.AppBarButton", "DynamicOverflowOrder");
}
const PropertyDescriptor& AppBarToggleButton::dynamicOverflowOrderProperty()
{
    return propertyFor("Velora.Fluent.AppBarToggleButton", "DynamicOverflowOrder");
}
i32 AppBarButton::dynamicOverflowOrder() const
{
    return static_cast<i32>(getValue(propertyFor(className(), "DynamicOverflowOrder")).asInt());
}
PropertySetResult AppBarButton::setDynamicOverflowOrder(i32 value)
{
    return setValue(propertyFor(className(), "DynamicOverflowOrder"), static_cast<i64>(value));
}
bool AppBarButton::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == propertyFor(className(), "DynamicOverflowOrder").id)
        return v.isInt() && v.asInt() >= 0 && v.asInt() <= std::numeric_limits<i32>::max();
    return FlyoutButton::validatePropertyValue(p, v);
}
std::string AppBarButton::label() const
{
    return content();
}
PropertySetResult AppBarButton::setLabel(std::string value)
{
    return setContent(std::move(value));
}
std::string AppBarButton::iconGlyph() const
{
    return getValue(propertyFor(className(), "IconGlyph")).asString();
}
PropertySetResult AppBarButton::setIconGlyph(std::string value)
{
    return setValue(propertyFor(className(), "IconGlyph"), std::move(value));
}
bool AppBarButton::isCompact() const
{
    return getValue(propertyFor(className(), "IsCompact")).asBool();
}
PropertySetResult AppBarButton::setIsCompact(bool value)
{
    return setValue(propertyFor(className(), "IsCompact"), value);
}
const PropertyDescriptor& AppBarToggleButton::isCheckedProperty()
{
    return propertyFor("Velora.Fluent.AppBarToggleButton", "IsChecked");
}
bool AppBarToggleButton::isChecked() const
{
    return getValue(isCheckedProperty()).asBool();
}
PropertySetResult AppBarToggleButton::setIsChecked(bool value)
{
    return setValue(isCheckedProperty(), value);
}
u64 AppBarToggleButton::addIsCheckedChanged(std::function<void(bool)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [this, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                             const PropertyValue&, const PropertyValue& value)
        {
            if (isInTree() && p.id == isCheckedProperty().id)
                handler(value.asBool());
        });
}
} // namespace velora
