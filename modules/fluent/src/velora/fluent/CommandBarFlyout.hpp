// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <velora/fluent/CommandBar.hpp>

namespace velora
{
// Standard context-command presentation. Collections contain the actual command objects.
class CommandBarFlyout final : public DependencyObject, public std::enable_shared_from_this<CommandBarFlyout>
{
  public:
    CommandBarFlyout();
    ~CommandBarFlyout();
    void appendPrimary(std::shared_ptr<UIElement>);
    void appendSecondary(std::shared_ptr<UIElement>);
    void clearPrimary();
    void clearSecondary();
    const std::vector<std::shared_ptr<UIElement>>& primaryCommands() const noexcept;
    const std::vector<std::shared_ptr<UIElement>>& secondaryCommands() const noexcept;
    bool showAt(Framework&, ElementId anchor);
    void hide();
    bool isOpen() const noexcept;
    const std::string& lastError() const noexcept;

  private:
    void append(std::shared_ptr<UIElement>, bool secondary);
    void requireEditable() const;
    ElementId buildPresenter(Framework&);
    void expand(bool);
    void navigate(KeyEventArgs&);
    void closed();
    std::vector<std::shared_ptr<UIElement>> m_primary, m_secondary;
    std::vector<EventSubscription> m_events;
    std::shared_ptr<Flyout> m_flyout;
    Framework* m_ui = nullptr;
    std::weak_ptr<void> m_lifetime;
    ElementId m_root = 0, m_primaryHost = 0, m_secondaryHost = 0, m_more = 0;
    bool m_expanded = true, m_opening = false;
};
} // namespace velora
