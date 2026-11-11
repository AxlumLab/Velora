// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <velora/controls/ContentElement.hpp>
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
class TabViewItem final : public ContentElement
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
    u64 addCloseRequested(std::function<void(const TabCloseRequestedEventArgs&)>);
    bool removeCloseRequested(u64);

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    std::string_view contentTemplateUri() const override;

  private:
    friend class TabView;
    void raiseCloseRequested(const TabCloseRequestedEventArgs&);
    u64 m_nextToken = 1;
    std::map<u64, std::function<void(const TabCloseRequestedEventArgs&)>> m_handlers;
};
} // namespace velora
