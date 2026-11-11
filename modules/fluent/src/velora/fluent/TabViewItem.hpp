// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
namespace velora
{
class TabViewItem;
struct TabCloseRequestedEventArgs
{
    std::shared_ptr<TabViewItem> tab;
    i32 index = -1;
};
class TabViewItem final : public UIElement
{
  public:
    TabViewItem();
    std::string className() const override;
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& headerProperty();
    static const PropertyDescriptor& isClosableProperty();
    std::string header() const;
    PropertySetResult setHeader(std::string);
    bool isClosable() const;
    PropertySetResult setIsClosable(bool);
    std::shared_ptr<UIElement> content() const;
    PropertySetResult setContent(std::shared_ptr<UIElement>);
    std::vector<std::shared_ptr<UIElement>> ownedContentObjects() const override;
    u64 addCloseRequested(std::function<void(const TabCloseRequestedEventArgs&)>);
    bool removeCloseRequested(u64);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;

  private:
    friend class TabView;
    void raiseCloseRequested(const TabCloseRequestedEventArgs&);
    void syncContent();
    Framework* m_ui = nullptr;
    ElementId m_element = 0, m_host = 0, m_content = 0;
    bool m_syncing = false;
    u64 m_nextToken = 1;
    std::map<u64, std::function<void(const TabCloseRequestedEventArgs&)>> m_handlers;
};
} // namespace velora
