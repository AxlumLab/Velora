// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/fluent/FluentElement.hpp>
namespace velora
{
class ScrollBar final : public RangeBaseElement
{
  public:
    ScrollBar();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    f64 viewportSize() const;
    PropertySetResult setViewportSize(f64);
    f64 smallChange() const;
    PropertySetResult setSmallChange(f64);
    f64 largeChange() const;
    PropertySetResult setLargeChange(f64);
    u64 addValueChanged(std::function<void(f64, f64)>);
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    bool renderOverride(Canvas&, Size) const override;
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onPointerMoved(InputContext&, PointerEventArgs&) override;
    void onPointerReleased(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    void onDetached() noexcept override;
    bool isFocusable() const noexcept override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    bool vertical() const;
    std::pair<f64, f64> thumb(f64 length) const;
    bool m_dragging = false;
    f64 m_grab = 0;
};
} // namespace velora
