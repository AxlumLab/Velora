#pragma once
#include <string>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
namespace velora
{
class IconElement : public UIElement
{
  public:
    [[nodiscard]] virtual std::string glyph() const = 0;
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    bool renderOverride(Canvas& canvas, Size size) const override;

  protected:
    IconElement();
};
class FontIcon final : public IconElement
{
  public:
    [[nodiscard]] std::string className() const override;
    [[nodiscard]] std::string glyph() const override;
    PropertySetResult setGlyph(std::string value);
    static const PropertyDescriptor& glyphProperty();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
};
} // namespace velora
