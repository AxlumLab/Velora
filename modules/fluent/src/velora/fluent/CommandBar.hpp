// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <velora/fluent/AppBarButton.hpp>

namespace velora
{
class AppBarSeparator final : public UIElement
{
  public:
    AppBarSeparator();
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    static const PropertyDescriptor& orientationProperty();
    [[nodiscard]] std::string orientation() const;
    PropertySetResult setOrientation(std::string value);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void updateLine();
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_line = 0;
};

class CommandBar final : public UIElement
{
  public:
    CommandBar();
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    static const PropertyDescriptor& isOpenProperty();
    [[nodiscard]] bool isOpen() const;
    PropertySetResult setIsOpen(bool value);
    void appendPrimary(std::shared_ptr<UIElement> command);
    void appendSecondary(std::shared_ptr<UIElement> command);
    void clearPrimary();
    void clearSecondary();
    [[nodiscard]] const std::vector<std::shared_ptr<UIElement>>& primaryCommands() const noexcept;
    [[nodiscard]] const std::vector<std::shared_ptr<UIElement>>& secondaryCommands() const noexcept;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    [[nodiscard]] std::optional<AutomationControlType> automationControlType() const noexcept override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void append(std::shared_ptr<UIElement>, bool secondary);
    void clear(bool secondary);
    void prepare(const std::shared_ptr<UIElement>& command, bool overflow);
    ElementId buildOverflow(Framework& ui);
    void syncOpen();
    void updateMore();
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_primaryHost = 0, m_more = 0;
    std::vector<std::shared_ptr<UIElement>> m_primary, m_secondary;
    std::vector<EventSubscription> m_events, m_overflowEvents;
    std::shared_ptr<Flyout> m_overflow;
    bool m_mutating = false, m_syncing = false;
};
} // namespace velora
