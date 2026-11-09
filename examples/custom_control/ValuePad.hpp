#pragma once

#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>

namespace velora
{

class Framework;
// A source control whose declaration is shared by the compiler and runtime factory.
class ValuePad : public UIElement
{
  public:
    ValuePad() noexcept;
    [[nodiscard]] std::string className() const override;
    [[nodiscard]] static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    [[nodiscard]] f64 value() const;
    PropertySetResult setValue(f64 value);
    using DependencyObject::setValue;
    [[nodiscard]] static const PropertyDescriptor& valueProperty();

    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    [[nodiscard]] bool isFocusable() const noexcept override;
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onPointerMoved(InputContext&, PointerEventArgs&) override;
    void onPointerReleased(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool renderOverride(Canvas&, Size) const override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;

  private:
    void setFromPointer(InputContext&, const PointerEventArgs&);
};
} // namespace velora
