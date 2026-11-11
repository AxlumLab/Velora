// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 TabViewItem.cpp/.idl; header presentation is owned by the TabView strip.
#include "FluentTypes.hpp"
#include <array>
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/TabViewItem.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
TabViewItem::TabViewItem() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == elementProperties().props.Content->id)
                syncContent();
        });
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
std::shared_ptr<UIElement> TabViewItem::content() const
{
    const auto& value = getValue(*elementProperties().props.Content);
    if (!value.isObject() || !value.asObject())
        return {};
    const auto& ref = value.asObject();
    return {ref.owner, static_cast<UIElement*>(ref.object)};
}
PropertySetResult TabViewItem::setContent(std::shared_ptr<UIElement> value)
{
    return setValue(*elementProperties().props.Content, ObjectRef::share<UIElement>(std::move(value)));
}
std::vector<std::shared_ptr<UIElement>> TabViewItem::ownedContentObjects() const
{
    const auto item = content();
    return item ? std::vector{item} : std::vector<std::shared_ptr<UIElement>>{};
}
bool TabViewItem::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == isClosableProperty().id)
        return value.isBool();
    if (p.id == headerProperty().id || (p.id == elementProperties().props.Content->id && value.isString()))
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
        return p.id != elementProperties().props.Content->id || !m_syncing;
    }
    if (p.id != elementProperties().props.Content->id)
        return true;
    if (m_syncing || !value.isObject())
        return false;
    const auto& ref = value.asObject();
    if (!ref)
        return true;
    if (!ref.owner || ref.owner.get() != ref.object || !ref.type || *ref.type != typeid(UIElement))
        return false;
    const auto* item = static_cast<UIElement*>(ref.object);
    return item->isOwnerThread() && !item->containsOwnedContent(*this) &&
           (!item->isInTree() || (m_ui && m_ui->tree().element(*item) == m_content));
}
void TabViewItem::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/TabViewItemView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("TabViewItem template: " + loaded.error);
    m_host = ui.tree().element(*loaded.root);
    ui.applyTemplate(self, m_host, {});
    syncContent();
}
void TabViewItem::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_host = m_content = 0;
}
void TabViewItem::syncContent()
{
    if (!m_ui || !m_host || m_syncing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    m_syncing = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_syncing};
    const auto item = content();
    if (item && m_ui->tree().element(*item) == m_content && m_content)
        return;
    const auto old = std::exchange(m_content, 0);
    if (old)
        m_ui->tree().remove(old);
    if (!m_ui)
        return;
    if (item)
    {
        m_content = m_ui->insert(item, m_host);
        if (!m_ui)
        {
            m_content = 0;
            return;
        }
        if (!m_content)
            throw std::runtime_error("Tab content failed to attach");
    }
    else
    {
        const auto& value = getValue(*elementProperties().props.Content);
        if (value.isString() && !value.asString().empty())
        {
            const auto text = value.asString();
            m_content = m_ui->create(ElementKind::kTextBlock, m_host);
            if (m_ui)
                m_ui->set(m_content, elementProperties().props.Text, text);
            if (!m_ui)
                m_content = 0;
        }
    }
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
