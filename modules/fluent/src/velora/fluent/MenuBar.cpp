// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// WinUI 2.3 MenuBar/MenuBarItem logic internalized into Velora's actual tree and Popup service.
#include "FluentTypes.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/MenuBar.hpp>

namespace velora
{
namespace
{
class Mutation
{
  public:
    explicit Mutation(bool& flag) : m_flag(flag)
    {
        if (m_flag)
            throw std::logic_error("MenuBar collection is already changing");
        m_flag = true;
    }
    ~Mutation()
    {
        m_flag = false;
    }

  private:
    bool& m_flag;
};
} // namespace

MenuBarItem::MenuBarItem() : FlyoutButton(Variant::kMenuBarItem)
{
    setMenu(std::make_shared<MenuFlyout>());
}
const XamlTypeDeclaration& MenuBarItem::declaration()
{
    return typeDeclaration("Velora.Fluent.MenuBarItem");
}
const PropertyDescriptor& MenuBarItem::headerProperty()
{
    return propertyFor("Velora.Fluent.MenuBarItem", "Header");
}
std::string MenuBarItem::registerType(Framework& ui)
{
    return registerControl(ui, declaration(), [] { return createElement<MenuBarItem>(); });
}
std::string MenuBarItem::header() const
{
    return content();
}
PropertySetResult MenuBarItem::setHeader(std::string value)
{
    return setContent(std::move(value));
}
std::shared_ptr<MenuFlyout> MenuBarItem::menu() const
{
    return std::dynamic_pointer_cast<MenuFlyout>(flyout());
}
void MenuBarItem::setMenu(std::shared_ptr<MenuFlyout> menu)
{
    if (!menu)
        throw std::invalid_argument("MenuBarItem requires a MenuFlyout");
    setFlyout(std::move(menu));
}
bool MenuBarItem::openFlyout()
{
    const auto content = menu();
    return content && !content->items().empty() && FlyoutButton::openFlyout();
}
ElementId MenuBarItem::flyoutPassThrough() const noexcept
{
    const auto bar = m_bar.lock();
    return bar ? bar->m_element : 0;
}
void MenuBarItem::onDetached() noexcept
{
    FlyoutButton::onDetached();
    m_bar.reset();
}
void MenuBarItem::onPointerMoved(InputContext&, PointerEventArgs& args)
{
    if (const auto bar = m_bar.lock())
    {
        const auto active = bar->activeItem();
        if (active && active.get() != this && openFlyout())
            args.handled = true;
    }
}
void MenuBarItem::onKeyDown(InputContext& context, KeyEventArgs& args)
{
    if ((args.key == Key::kDown || args.key == Key::kUp) && !args.ctrl)
    {
        if (openFlyout() && args.key == Key::kUp)
        {
            if (auto* ui = attachedFramework())
                ui->moveFocus(false);
        }
        args.handled = true;
        return;
    }
    FlyoutButton::onKeyDown(context, args);
}

MenuBar::MenuBar() : UIElement(ElementKind::kUserControl)
{
}
std::string MenuBar::className() const
{
    return "Velora.Fluent.MenuBar";
}
const XamlTypeDeclaration& MenuBar::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.MenuBar");
}
std::string MenuBar::registerType(Framework& ui)
{
    return ui.registerSourceControl(declaration(), [] { return createElement<MenuBar>(); }, {});
}
const std::vector<std::shared_ptr<MenuBarItem>>& MenuBar::items() const noexcept
{
    return m_items;
}
std::shared_ptr<MenuBarItem> MenuBar::activeItem() const
{
    for (const auto& item : m_items)
        if (item->isFlyoutOpen())
            return item;
    return {};
}
void MenuBar::attachItem(const std::shared_ptr<MenuBarItem>& item)
{
    if (!m_ui || !m_host)
        throw std::runtime_error("MenuBar host is unavailable");
    item->m_bar = std::dynamic_pointer_cast<MenuBar>(m_ui->tree().object(m_element));
    if (!m_ui->insert(item, m_host))
        throw std::runtime_error("MenuBarItem failed to attach");
}
void MenuBar::append(std::shared_ptr<MenuBarItem> item)
{
    if (!isOwnerThread() || !item || !item->isOwnerThread() || item->isInTree() ||
        std::ranges::find(m_items, item) != m_items.end())
        throw std::invalid_argument("MenuBar requires distinct detached UI-thread items");
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    Mutation changing(m_mutating);
    m_items.push_back(item);
    try
    {
        if (m_ui)
            attachItem(item);
    }
    catch (...)
    {
        m_items.pop_back();
        item->m_bar.reset();
        throw;
    }
}
void MenuBar::clear()
{
    if (!isOwnerThread())
        throw std::logic_error("MenuBar collection belongs to its UI thread");
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    Mutation changing(m_mutating);
    const auto items = std::exchange(m_items, {});
    for (const auto& item : items)
    {
        if (m_ui)
        {
            if (const auto id = m_ui->tree().element(*item))
                m_ui->tree().remove(id);
        }
        item->m_bar.reset();
    }
}
void MenuBar::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    Mutation changing(m_mutating);
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/MenuBarView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("MenuBar template: " + loaded.error);
    m_host = ui.tree().element(*loaded.names.at("ItemsHost"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {{"ItemsHost", m_host}});
    for (const auto& item : m_items)
        attachItem(item);
}
void MenuBar::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_host = 0;
    for (const auto& item : m_items)
        item->m_bar.reset();
}
bool MenuBar::navigate(Key key)
{
    if (!m_ui || m_items.empty())
        return false;
    const auto active = activeItem();
    usize index = 0;
    for (usize i = 0; i < m_items.size(); ++i)
    {
        const auto id = m_ui->tree().element(*m_items[i]);
        if (m_items[i] == active || m_ui->isWithin(m_ui->focused(), id))
        {
            index = i;
            break;
        }
    }
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto candidates = m_items;
    if (key == Key::kHome)
        index = candidates.size() - 1;
    else if (key == Key::kEnd)
        index = 0;
    const bool forward = key == Key::kRight || key == Key::kHome;
    for (usize count = 0; count < candidates.size(); ++count)
    {
        index =
            forward ? (index + 1) % candidates.size() : (index + candidates.size() - 1) % candidates.size();
        if (!m_ui)
            return false;
        const auto& item = candidates[index];
        const auto id = m_ui->tree().element(*item);
        if (!id || !m_ui->tree().property(id, *m_ui->props().IsEnabled).asBool() ||
            !m_ui->tree().property(id, *m_ui->props().Visibility).asBool())
            continue;
        if (active)
            return item->openFlyout();
        m_ui->focus(id);
        return true;
    }
    return false;
}
void MenuBar::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (!args.ctrl && !args.alt &&
        (args.key == Key::kLeft || args.key == Key::kRight || args.key == Key::kHome ||
         args.key == Key::kEnd))
        args.handled = navigate(args.key);
}
std::optional<AutomationControlType> MenuBar::automationControlType() const noexcept
{
    return AutomationControlType::kGroup;
}
} // namespace velora
