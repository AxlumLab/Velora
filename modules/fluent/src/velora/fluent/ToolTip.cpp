// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Velora implementation of the system ToolTip; theme reference: porting/ToolTip/.
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/ToolTip.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
ToolTip::ToolTip()
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == contentProperty().id && isOpen())
            {
                hide();
                show();
            }
        });
}
ToolTip::~ToolTip()
{
    detach();
}
const PropertyDescriptor& ToolTip::contentProperty()
{
    static PropertyRegistry registry;
    static const auto& property = registry.define("ToolTip.Content", "", PropertyFlags::kPaint);
    return property;
}
std::string ToolTip::content() const
{
    return getValue(contentProperty()).asString();
}
PropertySetResult ToolTip::setContent(std::string value)
{
    return setValue(contentProperty(), std::move(value));
}
bool ToolTip::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id != contentProperty().id)
        return true;
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
void ToolTip::attach(Framework& ui, ElementId anchor)
{
    if (!isOwnerThread() || !ui.isOwnerThread() || !ui.tree().contains(anchor) || weak_from_this().expired())
        throw std::invalid_argument(
            "ToolTip requires shared ownership and an attached anchor on its UI thread");
    detach();
    m_ui = &ui;
    m_lifetime = ui.lifetime();
    m_anchor = anchor;
    const auto weak = weak_from_this();
    m_flyout = std::make_shared<Flyout>(
        [weak](Framework& host)
        {
            const auto owner = weak.lock();
            if (!owner)
                throw std::runtime_error("ToolTip no longer exists");
            const auto loaded =
                loadComponent(host, *ResourceUri::parse("ms-appx-framework:///Controls/ToolTipView.vxbf"));
            if (!loaded.ok)
                throw std::runtime_error("ToolTip template: " + loaded.error);
            const auto root = host.tree().element(*loaded.root);
            host.set(root, host.props().Text, owner->content());
            return root;
        });
    m_hover = ui.tree().subscribe(
        [weak](ElementId element, const PropertyDescriptor& p)
        {
            const auto owner = weak.lock();
            if (!owner || !owner->m_ui || owner->m_lifetime.expired())
                return;
            auto& host = *owner->m_ui;
            if (p.id != host.props().IsPointerOver->id || !host.isWithin(element, owner->m_anchor))
                return;
            if (host.tree().property(element, p).asBool())
                owner->show();
            else
                owner->hide();
        });
    ui.tree().onRemoved(anchor,
                        [weak, anchor, host = &ui]
                        {
                            if (const auto owner = weak.lock();
                                owner && owner->m_anchor == anchor && owner->m_ui == host)
                                owner->detach();
                        });
}
void ToolTip::detach()
{
    if (!isOwnerThread())
        std::terminate();
    hide();
    if (m_ui && !m_lifetime.expired())
        m_ui->tree().unsubscribe(m_hover);
    m_ui = nullptr;
    m_anchor = 0;
    m_lifetime.reset();
    m_flyout.reset();
}
bool ToolTip::show()
{
    if (!isOwnerThread() || !m_ui || m_lifetime.expired() || content().empty() ||
        m_ui->tree().pointerCapture())
        return false;
    const auto flyout = m_flyout;
    if (flyout->isOpen())
        return true;
    if (m_ui->popupRoot())
        return false; // a hint must never replace an interactive menu or flyout
    return flyout->showAt(*m_ui, m_anchor, {}, 0, true);
}
void ToolTip::hide()
{
    const auto flyout = m_flyout;
    if (flyout)
        flyout->hide();
}
bool ToolTip::isOpen() const noexcept
{
    return m_flyout && m_flyout->isOpen();
}
} // namespace velora
