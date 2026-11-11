// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Shared internalization of WinUI 2.3 DropDownButton/SplitButton/ToggleSplitButton behavior.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <velora/controls/EventSubscription.hpp>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/fluent/Flyout.hpp>

namespace velora
{
class FlyoutButton : public UIElement
{
  public:
    [[nodiscard]] std::string className() const override;
    [[nodiscard]] std::string content() const;
    PropertySetResult setContent(std::string value);
    void setFlyout(std::shared_ptr<Flyout> flyout);
    [[nodiscard]] std::shared_ptr<Flyout> flyout() const noexcept;
    [[nodiscard]] bool isFlyoutOpen() const noexcept;
    virtual bool openFlyout();
    void closeFlyout();
    void invokePrimary();
    void setCommand(std::function<void()> command);
    [[nodiscard]] virtual bool isInOverflow() const noexcept;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    [[nodiscard]] bool isFocusable() const noexcept override;
    [[nodiscard]] std::string automationName() const override;
    [[nodiscard]] std::optional<AutomationControlType> automationControlType() const noexcept override;

  protected:
    enum class Variant
    {
        kDropDown,
        kSplit,
        kToggleSplit,
        kMenuBarItem,
        kAppBarButton,
        kAppBarToggleButton
    };
    explicit FlyoutButton(Variant variant);
    virtual ElementId flyoutPassThrough() const noexcept;
    Framework* attachedFramework() const noexcept;
    static const XamlTypeDeclaration& typeDeclaration(std::string_view name);
    static const PropertyDescriptor& propertyFor(std::string_view type, std::string_view name);
    static std::string registerControl(Framework&, const XamlTypeDeclaration&,
                                       SourceControlRegistry::Factory);
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void updateParts();
    ElementId part(std::string_view name) const;
    bool canInvoke() const;
    std::string_view contentPropertyName() const noexcept;
    const Variant m_variant;
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_root = 0;
    std::unordered_map<std::string, ElementId> m_parts;
    std::vector<EventSubscription> m_subscriptions;
    std::shared_ptr<Flyout> m_flyout; // C++ service attachment; no duplicate Flyout DP is exposed
    std::function<void()> m_command;
    bool m_overflowPresentation = false; // records the currently loaded template, not placement
};
} // namespace velora
