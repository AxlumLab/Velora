// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 TabViewItem.cpp/.idl; header presentation is owned by the TabView strip.
#include "FluentTypes.hpp"
#include <array>
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/fluent/TabViewItem.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
TabViewItem::TabViewItem() : ContentElement()
{
}
std::string TabViewItem::className() const
{
    return "Velora.Fluent.TabViewItem";
}
const XamlTypeDeclaration& TabViewItem::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.TabViewItem");
}
const PropertyDescriptor& TabViewItem::headerProperty()
{
    static PropertyRegistry registry;
    static const auto& p = registry.define("TabViewItem.Header", "", PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& TabViewItem::isClosableProperty()
{
    static PropertyRegistry registry;
    static const auto& p = registry.define("TabViewItem.IsClosable", true, PropertyFlags::kPaint);
    return p;
}
std::string TabViewItem::registerType(Framework& ui)
{
    const std::array properties{&headerProperty(), &isClosableProperty()};
    return ui.registerSourceControl(declaration(), [] { return createElement<TabViewItem>(); }, properties);
}
std::string TabViewItem::header() const
{
    return getValue(headerProperty()).asString();
}
PropertySetResult TabViewItem::setHeader(std::string value)
{
    return setValue(headerProperty(), std::move(value));
}
bool TabViewItem::isClosable() const
{
    return getValue(isClosableProperty()).asBool();
}
PropertySetResult TabViewItem::setIsClosable(bool value)
{
    return setValue(isClosableProperty(), value);
}

bool TabViewItem::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == isClosableProperty().id)
        return value.isBool();
    if (p.id == headerProperty().id)
    {
        if (!value.isString())
            return false;
        try
        {
            utf8ToUtf16(value.asString());
        }
        catch (const std::exception&)
        {
            return false;
        }
        return true;
    }
    return ContentElement::validatePropertyValue(p, value);
}
std::string_view TabViewItem::contentTemplateUri() const
{
    return "ms-appx-framework:///Controls/TabViewItemView.vxbf";
}

u64 TabViewItem::addCloseRequested(std::function<void(const TabCloseRequestedEventArgs&)> handler)
{
    if (!isOwnerThread())
        throw std::logic_error("TabViewItem events belong to its UI thread");
    if (!handler)
        return 0;
    if (!m_nextToken)
        throw std::overflow_error("TabViewItem event tokens exhausted");
    const auto token = m_nextToken++;
    m_handlers.emplace(token, std::move(handler));
    return token;
}
bool TabViewItem::removeCloseRequested(u64 token)
{
    return isOwnerThread() && m_handlers.erase(token) != 0;
}
void TabViewItem::raiseCloseRequested(const TabCloseRequestedEventArgs& args)
{
    const auto handlers = m_handlers;
    for (const auto& [token, handler] : handlers)
        if (m_handlers.contains(token))
            handler(args);
}
} // namespace velora
