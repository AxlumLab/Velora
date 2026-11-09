// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <velora/fluent/FlyoutButton.hpp>
#include <velora/fluent/MenuFlyout.hpp>

namespace velora
{
class MenuBar;
class MenuBarItem final : public FlyoutButton
{
  public:
    MenuBarItem();
    static const XamlTypeDeclaration& declaration();
    static const PropertyDescriptor& headerProperty();
    static std::string registerType(Framework& ui);
    [[nodiscard]] std::string header() const;
    PropertySetResult setHeader(std::string value);
    [[nodiscard]] std::shared_ptr<MenuFlyout> menu() const;
    void setMenu(std::shared_ptr<MenuFlyout> menu);
    bool openFlyout() override;
    void onDetached() noexcept override;
    void onPointerMoved(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;

  protected:
    ElementId flyoutPassThrough() const noexcept override;

  private:
    friend class MenuBar;
    std::weak_ptr<MenuBar> m_bar;
};

class MenuBar final : public UIElement
{
  public:
    MenuBar();
    [[nodiscard]] std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework& ui);
    void append(std::shared_ptr<MenuBarItem> item);
    void clear();
    [[nodiscard]] const std::vector<std::shared_ptr<MenuBarItem>>& items() const noexcept;
    [[nodiscard]] std::shared_ptr<MenuBarItem> activeItem() const;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    [[nodiscard]] std::optional<AutomationControlType> automationControlType() const noexcept override;

  private:
    friend class MenuBarItem;
    void attachItem(const std::shared_ptr<MenuBarItem>& item);
    bool navigate(Key key);
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_host = 0;
    std::vector<std::shared_ptr<MenuBarItem>> m_items;
    bool m_mutating = false;
};
} // namespace velora
