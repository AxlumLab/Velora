// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// MenuFlyout control logic is system-provided in WinUI 2.3; Velora implements it here.
#include "FluentTypes.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <variant>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/MenuFlyout.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
namespace
{
const XamlTypeDeclaration& menuType(std::string_view name)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    const auto* type = catalog.find(name);
    if (!type)
        throw std::runtime_error("missing menu type declaration");
    return *type;
}
const PropertyDescriptor& menuProperty(std::string_view type, std::string_view name)
{
    static PropertyRegistry registry;
    static const bool initialized = []
    {
        for (const auto* type : {"Velora.Fluent.MenuFlyoutItem", "Velora.Fluent.ToggleMenuFlyoutItem"})
            for (const auto& p : menuType(type).properties)
            {
                const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
                registry.define(p.runtimeName, std::get<PropertyValue>(parseValue(schema, p.defaultValue)),
                                PropertyFlags::kPaint);
            }
        return true;
    }();
    (void)initialized;
    return *registry.find(menuType(type).property(name)->runtimeName);
}
template <class T> std::string registerMenuType(Framework& ui)
{
    const auto& type = T::declaration();
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : type.properties)
        properties.push_back(&menuProperty(type.className, p.name));
    return ui.registerSourceControl(type, [] { return createElement<T>(); }, properties);
}
} // namespace

MenuFlyoutItem::MenuFlyoutItem() : MenuFlyoutItem(false)
{
}
MenuFlyoutItem::MenuFlyoutItem(bool toggle) : UIElement(ElementKind::kUserControl), m_toggle(toggle)
{
    addPropertyChanged([this](DependencyObject&, const PropertyDescriptor&, const PropertyValue&,
                              const PropertyValue&) { updateLabel(); });
}
ToggleMenuFlyoutItem::ToggleMenuFlyoutItem() : MenuFlyoutItem(true)
{
}
std::string MenuFlyoutItem::className() const
{
    return m_toggle ? "Velora.Fluent.ToggleMenuFlyoutItem" : "Velora.Fluent.MenuFlyoutItem";
}
const XamlTypeDeclaration& MenuFlyoutItem::declaration()
{
    return menuType("Velora.Fluent.MenuFlyoutItem");
}
const XamlTypeDeclaration& ToggleMenuFlyoutItem::declaration()
{
    return menuType("Velora.Fluent.ToggleMenuFlyoutItem");
}
const PropertyDescriptor& MenuFlyoutItem::textProperty()
{
    return menuProperty("Velora.Fluent.MenuFlyoutItem", "Text");
}
const PropertyDescriptor& ToggleMenuFlyoutItem::textProperty()
{
    return menuProperty("Velora.Fluent.ToggleMenuFlyoutItem", "Text");
}
const PropertyDescriptor& ToggleMenuFlyoutItem::isCheckedProperty()
{
    return menuProperty("Velora.Fluent.ToggleMenuFlyoutItem", "IsChecked");
}
std::string MenuFlyoutItem::registerType(Framework& ui)
{
    return registerMenuType<MenuFlyoutItem>(ui);
}
std::string ToggleMenuFlyoutItem::registerType(Framework& ui)
{
    return registerMenuType<ToggleMenuFlyoutItem>(ui);
}
std::string MenuFlyoutItem::text() const
{
    return getValue(menuProperty(className(), "Text")).asString();
}
PropertySetResult MenuFlyoutItem::setText(std::string value)
{
    return setValue(menuProperty(className(), "Text"), std::move(value));
}
void MenuFlyoutItem::setCommand(std::function<void()> command)
{
    if (!isOwnerThread())
        throw std::logic_error("MenuFlyoutItem command must be set on the UI owner thread");
    m_command = std::move(command);
}
bool ToggleMenuFlyoutItem::isChecked() const
{
    return getValue(isCheckedProperty()).asBool();
}
PropertySetResult ToggleMenuFlyoutItem::setIsChecked(bool value)
{
    return setValue(isCheckedProperty(), value);
}
bool MenuFlyoutItem::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == menuProperty(className(), "Text").id)
    {
        if (!value.isString())
            return false;
        try
        {
            utf8ToUtf16(value.asString());
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return !m_toggle || p.id != ToggleMenuFlyoutItem::isCheckedProperty().id || value.isBool();
}
void MenuFlyoutItem::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/MenuFlyoutItemView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("MenuFlyoutItem template: " + loaded.error);
    m_button = ui.tree().element(*loaded.names.at("ActionButton"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {{"ActionButton", m_button}});
    m_click = ui.subscribeEvent(m_button, ui.events().Click,
                                [this](ElementId, RoutedEventArgs& args)
                                {
                                    args.handled = true;
                                    invoke();
                                });
    updateLabel();
}
void MenuFlyoutItem::onDetached() noexcept
{
    m_click.reset();
    m_ui = nullptr;
    m_element = m_button = 0;
    m_owner.reset();
}
void MenuFlyoutItem::updateLabel()
{
    if (!m_ui)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const bool checked = m_toggle && getValue(ToggleMenuFlyoutItem::isCheckedProperty()).asBool();
    m_ui->set(m_button, m_ui->props().Content, (checked ? "✓  " : "    ") + text());
}
void MenuFlyoutItem::invoke()
{
    if (!m_ui)
        return;
    for (auto current = m_element; current; current = m_ui->tree().parent(current))
        if (!m_ui->tree().property(current, *m_ui->props().IsEnabled).asBool() ||
            !m_ui->tree().property(current, *m_ui->props().Visibility).asBool())
            return;
    const auto keepAlive = m_ui->tree().object(m_element);
    auto* host = m_ui;
    const auto lifetime = host->lifetime();
    const auto popup = host->popupRoot();
    const auto owner = m_owner.lock();
    const auto command = m_command;
    struct Dismiss
    {
        std::shared_ptr<Flyout> owner;
        Framework* host;
        std::weak_ptr<void> lifetime;
        ElementId popup;
        ~Dismiss()
        {
            if (owner && !lifetime.expired() && popup && host->popupRoot() == popup)
                owner->hide();
        }
    } dismiss{owner, host, lifetime, popup};
    if (m_toggle)
    {
        const auto& checked = ToggleMenuFlyoutItem::isCheckedProperty();
        setValue(checked, !getValue(checked).asBool());
    }
    if (m_ui)
        m_ui->tree().dispatch(m_element, m_ui->events().Click->id);
    if (command)
        command();
    // The scope closes the original popup even if a command throws, without closing a new one.
}
void MenuFlyoutItem::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (!m_ui)
        return;
    if (args.key == Key::kEnter || args.key == Key::kSpace)
        invoke();
    else if (args.key == Key::kUp || args.key == Key::kDown)
        m_ui->moveFocus(args.key == Key::kDown);
    else if (args.key == Key::kHome || args.key == Key::kEnd)
    {
        auto* host = m_ui;
        const auto lifetime = host->lifetime();
        host->focus(0);
        if (!lifetime.expired())
            host->moveFocus(args.key == Key::kHome);
    }
    else
        return;
    args.handled = true;
}
bool MenuFlyoutItem::isFocusable() const noexcept
{
    return true;
}
std::string MenuFlyoutItem::automationName() const
{
    return text();
}
std::optional<AutomationControlType> MenuFlyoutItem::automationControlType() const noexcept
{
    return AutomationControlType::kButton;
}

MenuFlyoutSeparator::MenuFlyoutSeparator() : UIElement(ElementKind::kUserControl)
{
}
std::string MenuFlyoutSeparator::className() const
{
    return "Velora.Fluent.MenuFlyoutSeparator";
}
const XamlTypeDeclaration& MenuFlyoutSeparator::declaration()
{
    return menuType("Velora.Fluent.MenuFlyoutSeparator");
}
std::string MenuFlyoutSeparator::registerType(Framework& ui)
{
    return registerMenuType<MenuFlyoutSeparator>(ui);
}
void MenuFlyoutSeparator::onAttached(Framework& ui, ElementId self)
{
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/MenuFlyoutSeparatorView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("MenuFlyoutSeparator template: " + loaded.error);
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {});
}

MenuFlyout::MenuFlyout() : Flyout([this](Framework& ui) { return buildContent(ui); })
{
}
void MenuFlyout::append(std::shared_ptr<UIElement> item)
{
    if (isOpen() || m_building || !item || !item->isOwnerThread() || item->isInTree() ||
        (!std::dynamic_pointer_cast<MenuFlyoutItem>(item) &&
         !std::dynamic_pointer_cast<MenuFlyoutSeparator>(item)) ||
        std::ranges::find(m_items, item) != m_items.end())
        throw std::invalid_argument(
            "MenuFlyout requires distinct detached menu items; edit only while closed");
    m_items.push_back(std::move(item));
}
void MenuFlyout::clear()
{
    if (isOpen() || m_building)
        throw std::logic_error("Close MenuFlyout before changing items");
    m_items.clear();
}
const std::vector<std::shared_ptr<UIElement>>& MenuFlyout::items() const noexcept
{
    return m_items;
}
ElementId MenuFlyout::buildContent(Framework& ui)
{
    m_building = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_building};
    const auto panel = ui.create(ElementKind::kStackPanel);
    try
    {
        ui.set(panel, ui.props().MinWidth, 200.0);
        for (const auto& item : m_items)
        {
            if (item->isInTree())
                throw std::runtime_error("MenuFlyout item is already attached");
            if (const auto action = std::dynamic_pointer_cast<MenuFlyoutItem>(item))
                action->m_owner = weak_from_this();
            if (!ui.insert(item, panel))
                throw std::runtime_error("MenuFlyout item could not attach");
        }
        return panel;
    }
    catch (...)
    {
        ui.tree().remove(panel);
        throw;
    }
}
} // namespace velora
