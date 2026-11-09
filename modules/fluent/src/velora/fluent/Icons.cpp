#include "FluentTypes.hpp"
#include <array>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/SymbolIcon.hpp>
#include <velora/graphics/Canvas.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
namespace
{
const XamlTypeDeclaration& iconType(std::string_view name)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find(name);
}
char16_t symbolGlyph(Symbol symbol)
{
    switch (symbol)
    {
    case Symbol::kAccept:
        return 0xE73E;
    case Symbol::kAdd:
        return 0xE710;
    case Symbol::kBack:
        return 0xE72B;
    case Symbol::kForward:
        return 0xE72A;
    case Symbol::kCancel:
        return 0xE711;
    case Symbol::kDelete:
        return 0xE74D;
    case Symbol::kEdit:
        return 0xE70F;
    case Symbol::kFind:
        return 0xE721;
    case Symbol::kMore:
        return 0xE712;
    case Symbol::kPeople:
        return 0xE716;
    case Symbol::kContact:
        return 0xE77B;
    case Symbol::kPin:
        return 0xE718;
    case Symbol::kSave:
        return 0xE74E;
    case Symbol::kSetting:
        return 0xE713;
    }
    return 0;
}
} // namespace
IconElement::IconElement() : UIElement(ElementKind::kTextBlock)
{
    const auto& p = elementProperties().props;
    setValue(*p.FontFamily, "Segoe MDL2 Assets", PropertySource::kDefaultStyle);
    setValue(*p.FontSize, 20.0, PropertySource::kDefaultStyle);
}
Size IconElement::measureOverride(LayoutContext&, ElementId, const MeasureInput& input)
{
    return {input.fontSize + input.insetX, lineHeight(input.fontSize) + input.insetY};
}
bool IconElement::renderOverride(Canvas& canvas, Size size) const
{
    const auto& p = elementProperties().props;
    const auto font = static_cast<f32>(getValue(*p.FontSize).asDouble());
    const auto argb = static_cast<u32>(getValue(*p.Foreground).asInt());
    const auto channel = [&](u32 shift) { return static_cast<f32>((argb >> shift) & 255) / 255.0f; };
    canvas.drawText({0, 0, size.width, size.height}, glyph(), font,
                    {Color{channel(16), channel(8), channel(0), channel(24)}},
                    getValue(*p.FontFamily).asString());
    return true;
}
std::string FontIcon::className() const
{
    return "Velora.Fluent.FontIcon";
}
std::string SymbolIcon::className() const
{
    return "Velora.Fluent.SymbolIcon";
}
const XamlTypeDeclaration& FontIcon::declaration()
{
    return iconType("Velora.Fluent.FontIcon");
}
const XamlTypeDeclaration& SymbolIcon::declaration()
{
    return iconType("Velora.Fluent.SymbolIcon");
}
const PropertyDescriptor& FontIcon::glyphProperty()
{
    static PropertyRegistry registry;
    static const auto& property =
        registry.define("FontIcon.Glyph", PropertyValue(std::string{}), PropertyFlags::kPaint);
    return property;
}
const PropertyDescriptor& SymbolIcon::symbolProperty()
{
    static PropertyRegistry registry;
    static const auto& property = registry.define("SymbolIcon.Symbol", i64{57611}, PropertyFlags::kPaint);
    return property;
}
std::string FontIcon::glyph() const
{
    return getValue(glyphProperty()).asString();
}
PropertySetResult FontIcon::setGlyph(std::string value)
{
    return setValue(glyphProperty(), std::move(value));
}
Symbol SymbolIcon::symbol() const
{
    return static_cast<Symbol>(getValue(symbolProperty()).asInt());
}
PropertySetResult SymbolIcon::setSymbol(Symbol value)
{
    return setValue(symbolProperty(), static_cast<i64>(value));
}
std::string SymbolIcon::glyph() const
{
    return utf16ToUtf8(std::u16string(1, symbolGlyph(symbol())));
}
bool FontIcon::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id != glyphProperty().id)
        return true;
    if (!value.isString())
        return false;
    try
    {
        utf8ToUtf16(value.asString());
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}
bool SymbolIcon::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id != symbolProperty().id)
        return true;
    return value.isInt() && value.asInt() >= 0 && value.asInt() <= 65535 &&
           symbolGlyph(static_cast<Symbol>(value.asInt())) != 0;
}
std::string FontIcon::registerType(Framework& ui)
{
    const std::array properties = {&glyphProperty()};
    return ui.registerSourceControl(declaration(), [] { return createElement<FontIcon>(); }, properties);
}
std::string SymbolIcon::registerType(Framework& ui)
{
    const std::array properties = {&symbolProperty()};
    return ui.registerSourceControl(declaration(), [] { return createElement<SymbolIcon>(); }, properties);
}
} // namespace velora
