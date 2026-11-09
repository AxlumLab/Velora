#pragma once
#include <velora/fluent/FontIcon.hpp>
namespace velora
{
// Stable UWP Symbol values from the Windows SDK; render corresponding MDL2 glyphs.
enum class Symbol : i32
{
    kEdit = 57604,
    kSave = 57605,
    kDelete = 57607,
    kAdd = 57609,
    kCancel = 57610,
    kAccept = 57611,
    kMore = 57612,
    kForward = 57617,
    kBack = 57618,
    kSetting = 57621,
    kFind = 57626,
    kPeople = 57637,
    kContact = 57661,
    kPin = 57665
};
class SymbolIcon final : public IconElement
{
  public:
    [[nodiscard]] std::string className() const override;
    [[nodiscard]] std::string glyph() const override;
    [[nodiscard]] Symbol symbol() const;
    PropertySetResult setSymbol(Symbol value);
    static const PropertyDescriptor& symbolProperty();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
};
} // namespace velora
