// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 NavigationViewItem.cpp/.idl; initial flat navigation presentation.
#include "FluentTypes.hpp"
#include <array>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/NavigationViewItem.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
namespace
{
const ReadOnlyPropertyKey& selectedKey()
{
    static PropertyRegistry registry;
    static const auto key = []
    {
        PropertyDescriptor p;
        p.name = "NavigationViewItem.IsSelected";
        p.defaultValue = false;
        p.flags = PropertyFlags::kPaint;
        return registry.defineReadOnly(std::move(p));
    }();
    return key;
}
} // namespace
NavigationViewItem::NavigationViewItem() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged([this](DependencyObject&, const PropertyDescriptor&, const PropertyValue&,
                              const PropertyValue&) { updateParts(); });
}
std::string NavigationViewItem::className() const
{
    return "Velora.Fluent.NavigationViewItem";
}
const XamlTypeDeclaration& NavigationViewItem::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.NavigationViewItem");
}
const PropertyDescriptor& NavigationViewItem::iconGlyphProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("NavigationViewItem.IconGlyph", "", PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& NavigationViewItem::selectsOnInvokedProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("NavigationViewItem.SelectsOnInvoked", true, PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& NavigationViewItem::isSelectedProperty()
{
    return selectedKey().property();
}
std::string NavigationViewItem::registerType(Framework& ui)
{
    const std::array properties{&iconGlyphProperty(), &selectsOnInvokedProperty()};
    return ui.registerSourceControl(
        declaration(), [] { return createElement<NavigationViewItem>(); }, properties);
}
std::string NavigationViewItem::content() const
{
    return getValue(*elementProperties().props.Content).asString();
}
PropertySetResult NavigationViewItem::setContent(std::string value)
{
    return setValue(*elementProperties().props.Content, std::move(value));
}
std::string NavigationViewItem::iconGlyph() const
{
    return getValue(iconGlyphProperty()).asString();
}
PropertySetResult NavigationViewItem::setIconGlyph(std::string value)
{
    return setValue(iconGlyphProperty(), std::move(value));
}
bool NavigationViewItem::selectsOnInvoked() const
{
    return getValue(selectsOnInvokedProperty()).asBool();
}
PropertySetResult NavigationViewItem::setSelectsOnInvoked(bool value)
{
    return setValue(selectsOnInvokedProperty(), value);
}
bool NavigationViewItem::isSelected() const
{
    return getValue(isSelectedProperty()).asBool();
}
bool NavigationViewItem::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == selectsOnInvokedProperty().id)
        return value.isBool();
    if (p.id != iconGlyphProperty().id && p.id != elementProperties().props.Content->id)
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
void NavigationViewItem::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/NavigationViewItemView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("NavigationViewItem template: " + loaded.error);
    m_button = ui.tree().element(*loaded.names.at("ActionButton"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {{"ActionButton", m_button}});
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    m_click = ui.subscribeEvent(m_button, ui.events().Click,
                                [weak](ElementId, RoutedEventArgs& args)
                                {
                                    args.handled = true;
                                    if (const auto owner = weak.lock())
                                        static_cast<NavigationViewItem&>(*owner).invoke();
                                });
    updateParts();
}
void NavigationViewItem::onDetached() noexcept
{
    m_click.reset();
    m_ui = nullptr;
    m_element = m_button = 0;
}
void NavigationViewItem::setPresentation(bool selected, bool compact)
{
    m_compact = compact;
    setValue(selectedKey(), selected);
    updateParts();
}
void NavigationViewItem::updateParts()
{
    if (!m_ui || !m_button)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto& props = elementProperties().props;
    m_ui->set(m_button, props.Content, content());
    if (!m_ui)
        return;
    const auto icon = m_ui->templateChild(m_button, "ItemIcon");
    const auto caption = m_ui->templateChild(m_button, "Caption");
    if (icon)
        m_ui->set(icon, props.Text, iconGlyph());
    if (m_ui && caption)
        m_ui->set(caption, props.Visibility, !m_compact);
    if (m_ui && caption)
        m_ui->set(caption, props.FontSize, isSelected() ? 16.0 : 14.0);
}
void NavigationViewItem::invoke()
{
    if (!m_ui)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    for (auto current = m_element; current; current = m_ui->tree().parent(current))
        if (!m_ui->tree().property(current, *m_ui->props().IsEnabled).asBool() ||
            !m_ui->tree().property(current, *m_ui->props().Visibility).asBool())
            return;
    m_ui->focus(m_element);
    if (m_ui)
        m_ui->tree().dispatch(m_element, m_ui->events().Click->id);
}
void NavigationViewItem::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.key == Key::kEnter || args.key == Key::kSpace)
    {
        invoke();
        args.handled = true;
    }
}
bool NavigationViewItem::isFocusable() const noexcept
{
    return true;
}
std::string NavigationViewItem::automationName() const
{
    return content();
}
} // namespace velora
