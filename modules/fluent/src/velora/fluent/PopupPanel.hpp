// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <velora/fluent/Flyout.hpp>

namespace velora
{
enum class PanelButton : u8
{
    kPrimary,
    kSecondary,
    kClose
};
enum class PanelResult : u8
{
    kNone,
    kPrimary,
    kSecondary
};
enum class PanelCloseReason : u8
{
    kProgrammatic,
    kCloseButton,
    kLightDismiss
};
struct PanelButtonEventArgs
{
    PanelButton button;
    bool cancel = false;
};
struct PanelClosingEventArgs
{
    PanelCloseReason reason;
    PanelResult result;
    bool cancel = false;
};

// UI-thread popup services use compiled presenters and the window's existing popup lifetime.
class PopupPanel : public DependencyObject, public std::enable_shared_from_this<PopupPanel>
{
  public:
    ~PopupPanel() override;
    std::string title() const;
    PropertySetResult setTitle(std::string);
    std::string content() const;
    PropertySetResult setContent(std::string);
    bool isOpen() const noexcept;
    bool hide();
    PanelResult result() const noexcept;
    const std::string& lastError() const noexcept;
    u64 addButtonClick(std::function<void(PanelButtonEventArgs&)>);
    u64 addClosing(std::function<void(PanelClosingEventArgs&)>);
    u64 addClosed(std::function<void(PanelCloseReason, PanelResult)>);
    bool removeHandler(u64);

  protected:
    explicit PopupPanel(bool teaching);
    bool showPanel(Framework&, ElementId);
    void attachTarget(Framework&, ElementId);
    void detachTarget();
    static const PropertyDescriptor& panelProperty(bool teaching, std::string_view);
    const PropertyDescriptor& property(std::string_view) const;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    ElementId buildPresenter(Framework&);
    void updateParts();
    void syncOpen();
    void writeOpen(bool);
    void closed();
    void invoke(PanelButton);
    bool canClose(PanelCloseReason, PanelResult);
    bool requestClose(PanelCloseReason, PanelResult);
    u64 nextToken();
    const bool m_teaching;
    Framework* m_ui = nullptr;
    std::weak_ptr<void> m_lifetime;
    ElementId m_anchor = 0, m_root = 0;
    std::map<std::string, ElementId, std::less<>> m_parts;
    std::vector<EventSubscription> m_events;
    std::shared_ptr<Flyout> m_flyout;
    bool m_syncing = false, m_notifying = false;
    PanelResult m_result = PanelResult::kNone;
    PanelCloseReason m_reason = PanelCloseReason::kProgrammatic;
    u64 m_nextToken = 1;
    std::map<u64, std::function<void(PanelButtonEventArgs&)>> m_buttons;
    std::map<u64, std::function<void(PanelClosingEventArgs&)>> m_closing;
    std::map<u64, std::function<void(PanelCloseReason, PanelResult)>> m_closed;
};
} // namespace velora
