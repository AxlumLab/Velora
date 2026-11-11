// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <velora/fluent/NavigationViewItem.hpp>
#include <velora/fluent/PaneLayout.hpp>
namespace velora
{
struct NavigationItemInvokedEventArgs
{
    std::shared_ptr<NavigationViewItem> item;
    bool isSettingsInvoked = false;
};
class NavigationView final : public PaneLayout
{
  public:
    NavigationView();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    void setMenuItems(std::vector<std::shared_ptr<NavigationViewItem>>);
    const std::vector<std::shared_ptr<NavigationViewItem>>& menuItems() const noexcept;
    std::shared_ptr<NavigationViewItem> settingsItem() const;
    static const PropertyDescriptor& selectedItemProperty();
    std::shared_ptr<NavigationViewItem> selectedItem() const;
    PropertySetResult setSelectedItem(std::shared_ptr<NavigationViewItem>);
    std::shared_ptr<UIElement> content() const;
    PropertySetResult setContent(std::shared_ptr<UIElement>);
    bool isPaneOpen() const;
    PropertySetResult setIsPaneOpen(bool);
    std::string paneDisplayMode() const;
    PropertySetResult setPaneDisplayMode(std::string);
    std::string paneTitle() const;
    PropertySetResult setPaneTitle(std::string);
    f64 compactModeThresholdWidth() const;
    PropertySetResult setCompactModeThresholdWidth(f64);
    f64 expandedModeThresholdWidth() const;
    PropertySetResult setExpandedModeThresholdWidth(f64);
    f64 compactPaneLength() const;
    PropertySetResult setCompactPaneLength(f64);
    f64 openPaneLength() const;
    PropertySetResult setOpenPaneLength(f64);
    bool isSettingsVisible() const;
    PropertySetResult setIsSettingsVisible(bool);
    bool isBackEnabled() const;
    PropertySetResult setIsBackEnabled(bool);
    std::string isBackButtonVisible() const;
    PropertySetResult setIsBackButtonVisible(std::string);
    bool isPaneToggleButtonVisible() const;
    PropertySetResult setIsPaneToggleButtonVisible(bool);
    static const PropertyDescriptor& displayModeProperty();
    std::string displayMode() const;
    u64 addSelectionChanged(std::function<void(std::shared_ptr<NavigationViewItem>)>);
    u64 addDisplayModeChanged(std::function<void(std::string)>);
    u64 addItemInvoked(std::function<void(const NavigationItemInvokedEventArgs&)>);
    u64 addBackRequested(std::function<void()>);
    bool removeHandler(u64);
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
    void onPointerPressed(InputContext&, PointerEventArgs&) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;

  protected:
    const PropertyDescriptor& paneProperty(usize) const override;
    PaneLayoutPlan layoutPanes(Size, const std::array<Size, 2>&) override;
    bool validateLayoutProperty(const PropertyDescriptor&, const PropertyValue&) const override;
    void onPlanArranged(const PaneLayoutPlan&) override;

  private:
    const PropertyDescriptor& property(std::string_view) const;
    void rebuildMenu();
    void updateParts();
    void invokeItem(const std::shared_ptr<NavigationViewItem>&);
    bool paneOpenFor(std::string_view) const;
    u64 nextToken();
    std::vector<std::shared_ptr<NavigationViewItem>> m_items;
    std::shared_ptr<NavigationViewItem> m_settings;
    std::vector<EventSubscription> m_events, m_menuEvents;
    ElementId m_element = 0, m_menuHost = 0, m_chrome = 0, m_toggle = 0, m_back = 0, m_title = 0;
    bool m_busy = false, m_updating = false, m_adapting = false, m_firstLayout = true, m_forceClosed = false;
    u64 m_nextToken = 1;
    std::map<u64, std::function<void(const NavigationItemInvokedEventArgs&)>> m_invoked;
    std::map<u64, std::function<void()>> m_backHandlers;
};
} // namespace velora
