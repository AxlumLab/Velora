#pragma once

#include <velora/controls/UIElement.hpp>

namespace velora
{

// A source control using the current UserControl schema identity. It does not add an enum,
// proxy or second property store; extensible XAML type registration is a later step.
class ValuePad : public UIElement
{
  public:
    ValuePad() noexcept;
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
