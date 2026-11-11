// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>

namespace velora
{
class RatingControl final : public UIElement
{
  public:
    RatingControl();
    using DependencyObject::setValue;
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& valueProperty();
    static const PropertyDescriptor& maxRatingProperty();
    static const PropertyDescriptor& placeholderValueProperty();
    [[nodiscard]] f64 value() const;
    PropertySetResult setValue(f64);
    [[nodiscard]] f64 placeholderValue() const;
    PropertySetResult setPlaceholderValue(f64);
    [[nodiscard]] i32 maxRating() const;
    PropertySetResult setMaxRating(i32);
    [[nodiscard]] i32 initialSetValue() const;
    PropertySetResult setInitialSetValue(i32);
    [[nodiscard]] bool isReadOnly() const;
    PropertySetResult setIsReadOnly(bool);
    [[nodiscard]] bool isClearEnabled() const;
    PropertySetResult setIsClearEnabled(bool);
    [[nodiscard]] std::string caption() const;
    PropertySetResult setCaption(std::string);
    // User actions only; programmatic changes remain observable through addPropertyChanged.
    u64 addValueChanged(std::function<void(f64)>);
    bool removeValueChanged(u64);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onPointerMoved(InputContext&, PointerEventArgs&) override;
    void onPointerReleased(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    [[nodiscard]] bool isFocusable() const noexcept override;
    [[nodiscard]] std::string automationName() const override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;

  private:
    static const PropertyDescriptor& property(std::string_view);
    void updateParts();
    void userRate(f64, bool mouse);
    f64 pointerRating(f32 x) const;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_background = 0, m_foreground = 0, m_clip = 0, m_caption = 0;
    i32 m_starCount = 0;
    f64 m_preview = -1;
    bool m_dragging = false;
    std::map<u64, std::function<void(f64)>> m_handlers;
    u64 m_nextHandler = 1;
};
} // namespace velora
