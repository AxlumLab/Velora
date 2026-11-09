// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
// Internal implementation of system menu behavior and WinUI 2.3 menu theme contracts.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/fluent/Flyout.hpp>

namespace velora
{
class MenuFlyout;
class MenuFlyoutItem : public UIElement
{
  public:
    MenuFlyoutItem();
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    static const PropertyDescriptor& textProperty();
    [[nodiscard]] std::string text() const;
    PropertySetResult setText(std::string value);
    [[nodiscard]] std::string iconGlyph() const;
    PropertySetResult setIconGlyph(std::string value);
    [[nodiscard]] std::string keyboardAcceleratorTextOverride() const;
    PropertySetResult setKeyboardAcceleratorTextOverride(std::string value);
    void setCommand(std::function<void()> command);
    void invoke();
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    [[nodiscard]] bool isFocusable() const noexcept override;
    [[nodiscard]] std::string automationName() const override;
    [[nodiscard]] std::optional<AutomationControlType> automationControlType() const noexcept override;

  protected:
    explicit MenuFlyoutItem(bool toggle, bool radio = false);
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    friend class MenuFlyout;
    friend class RadioMenuFlyoutItem;
    void updateLabel();
    const bool m_toggle;
    const bool m_radio;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_button = 0;
    EventSubscription m_click;
    std::weak_ptr<Flyout> m_owner;
    std::function<void()> m_command;
};

class ToggleMenuFlyoutItem final : public MenuFlyoutItem
{
  public:
    ToggleMenuFlyoutItem();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    static const PropertyDescriptor& textProperty();
    static const PropertyDescriptor& isCheckedProperty();
    [[nodiscard]] bool isChecked() const;
    PropertySetResult setIsChecked(bool value);
};

class MenuFlyoutSeparator final : public UIElement
{
  public:
    MenuFlyoutSeparator();
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    void onAttached(Framework&, ElementId) override;
};

// UI thread collection of actual item objects. Edit only while closed. A new panel is made
// per opening; each item is reattached, retaining its own dependency-property values.
class MenuFlyout final : public Flyout
{
  public:
    MenuFlyout();
    void append(std::shared_ptr<UIElement> item);
    void clear();
    [[nodiscard]] const std::vector<std::shared_ptr<UIElement>>& items() const noexcept;

  private:
    ElementId buildContent(Framework& ui);
    std::vector<std::shared_ptr<UIElement>> m_items;
    bool m_building = false;
};
} // namespace velora
