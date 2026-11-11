// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Shared hosting extracted from Velora's WinUI 2.3 TabViewItem adaptation.
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/ContentElement.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
ContentElement::ContentElement() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (m_ui && p.id == hostedContentProperty().id)
                syncContent();
        });
}
const PropertyDescriptor& ContentElement::hostedContentProperty() const
{
    return *elementProperties().props.Content;
}
std::string_view ContentElement::contentHostName() const
{
    return {};
}
std::shared_ptr<UIElement> ContentElement::content() const
{
    const auto& value = getValue(hostedContentProperty());
    if (!value.isObject() || !value.asObject())
        return {};
    const auto& ref = value.asObject();
    return {ref.owner, static_cast<UIElement*>(ref.object)};
}
PropertySetResult ContentElement::setContent(std::shared_ptr<UIElement> value)
{
    return setValue(hostedContentProperty(), ObjectRef::share<UIElement>(std::move(value)));
}
std::vector<std::shared_ptr<UIElement>> ContentElement::ownedContentObjects() const
{
    const auto item = content();
    return item ? std::vector{item} : std::vector<std::shared_ptr<UIElement>>{};
}
bool ContentElement::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    const auto& hosted = hostedContentProperty();
    if (p.id != hosted.id)
    {
        if (hosted.id != elementProperties().props.Content->id &&
            p.id == elementProperties().props.Content->id)
            return value.isString() && value.asString().empty();
        return true;
    }
    if (m_syncing)
        return false;
    if (value.isString())
    {
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
    if (!value.isObject())
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
void ContentElement::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto uri = ResourceUri::parse(contentTemplateUri());
    if (!uri)
        throw std::invalid_argument("Invalid content template URI");
    const auto loaded = loadComponent(ui, *uri, options);
    if (!loaded.ok)
        throw std::runtime_error("Content template: " + loaded.error);
    m_host = ui.tree().element(*loaded.root);
    ui.applyTemplate(self, m_host, {});
    if (!contentHostName().empty())
        m_host = ui.tree().element(*loaded.names.at(std::string(contentHostName())));
    syncContent();
}
void ContentElement::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_host = m_content = 0;
}
Framework* ContentElement::attachedFramework() const noexcept
{
    return m_ui;
}
ElementId ContentElement::contentHost() const noexcept
{
    return m_host;
}
ElementId ContentElement::contentNode() const noexcept
{
    return m_content;
}
ElementId ContentElement::ownerElement() const noexcept
{
    return m_element;
}
void ContentElement::syncContent()
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
            throw std::runtime_error("Content failed to attach");
    }
    else
    {
        const auto& value = getValue(hostedContentProperty());
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
} // namespace velora
