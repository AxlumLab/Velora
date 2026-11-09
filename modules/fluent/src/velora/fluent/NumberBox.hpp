// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>

namespace velora
{
// Direct internalization of WinUI 2.3's numeric editing and inline spin behavior.
// Compact popup placement, custom number formatters and wheel input remain unsupported.
class NumberBox final : public UIElement
{
  public:
    NumberBox();
    [[nodiscard]] std::string className() const override;
    [[nodiscard]] static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
#define VELORA_NUMBER_PROPERTY(type, method, setter)                                                         \
    [[nodiscard]] type method() const;                                                                       \
    PropertySetResult setter(type value);                                                                    \
    [[nodiscard]] static const PropertyDescriptor& method##Property();
    VELORA_NUMBER_PROPERTY(f64, value, setValue)
    VELORA_NUMBER_PROPERTY(f64, minimum, setMinimum)
    VELORA_NUMBER_PROPERTY(f64, maximum, setMaximum)
    VELORA_NUMBER_PROPERTY(f64, smallChange, setSmallChange)
    VELORA_NUMBER_PROPERTY(f64, largeChange, setLargeChange)
    VELORA_NUMBER_PROPERTY(bool, isWrapEnabled, setIsWrapEnabled)
    VELORA_NUMBER_PROPERTY(bool, acceptsExpression, setAcceptsExpression)
    VELORA_NUMBER_PROPERTY(std::string, text, setText)
    VELORA_NUMBER_PROPERTY(std::string, header, setHeader)
    VELORA_NUMBER_PROPERTY(std::string, placeholderText, setPlaceholderText)
    VELORA_NUMBER_PROPERTY(std::string, validationMode, setValidationMode)
    VELORA_NUMBER_PROPERTY(std::string, spinButtonPlacementMode, setSpinButtonPlacementMode)
#undef VELORA_NUMBER_PROPERTY
    using UIElement::setValue;
    // Token removed with removePropertyChanged; callback sees final normalized old/new values.
    u64 addValueChanged(std::function<void(f64, f64)> handler);
    void commitInput();
    void cancelInput();
    void stepValue(f64 change);
    void onAttached(Framework& ui, ElementId self) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs& args) override;
    [[nodiscard]] std::string automationName() const override;
    [[nodiscard]] std::optional<AutomationControlType> automationControlType() const noexcept override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;

  private:
    static const PropertyDescriptor& property(std::string_view name);
    static std::string format(f64 value);
    void updateParts(bool updateText);
    ElementId part(std::string_view name) const;
    Framework* m_ui = nullptr;
    ElementId m_element = 0;
    std::unordered_map<std::string, ElementId> m_parts;
    std::vector<EventSubscription> m_subscriptions;
};
} // namespace velora
