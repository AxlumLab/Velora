// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <velora/fluent/PageSelector.hpp>
#include <velora/fluent/TabViewItem.hpp>
namespace velora
{
class TabView final : public PageSelector
{
  public:
    TabView();
    ~TabView() override;
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    void setTabItems(std::vector<std::shared_ptr<TabViewItem>>);
    std::vector<std::shared_ptr<TabViewItem>> tabItems() const;
    bool removeTab(const std::shared_ptr<TabViewItem>&);
    bool requestClose(i32 index);
    bool isAddTabButtonVisible() const;
    PropertySetResult setIsAddTabButtonVisible(bool);
    u64 addTabCloseRequested(std::function<void(const TabCloseRequestedEventArgs&)>);
    u64 addAddTabButtonClick(std::function<void()>);
    bool removeHandler(u64);
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;

  protected:
    bool acceptsPage(const SelectorPage&) const override;
    std::string pageHeader(usize) const override;
    ElementId createHeader(Framework&, ElementId parent, usize index) override;
    void onRebuildingHeaders() override;
    void onHeadersRebuilt(Framework&, ElementId parent) override;
    bool hasPivotProperties() const noexcept override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    void clearHeaderResources();
    void invokeAdd();
    void updateHeader(usize, const std::shared_ptr<TabViewItem>&);
    u64 nextToken();
    ElementId m_add = 0;
    std::vector<ElementId> m_captions, m_closeButtons;
    std::vector<EventSubscription> m_tabEvents;
    std::vector<std::pair<std::weak_ptr<TabViewItem>, u64>> m_itemSubscriptions;
    std::map<u64, std::function<void(const TabCloseRequestedEventArgs&)>> m_closeHandlers;
    std::map<u64, std::function<void()>> m_addHandlers;
    u64 m_nextToken = 1;
};
} // namespace velora
