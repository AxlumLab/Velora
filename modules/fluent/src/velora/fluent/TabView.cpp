// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 TabView.cpp/.idl: close-request ordering, add requests and selection retention.
#include <algorithm>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>
#include <velora/fluent/TabView.hpp>
namespace velora
{
TabView::TabView() : PageSelector(true)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id == property("IsAddTabButtonVisible").id)
                if (auto* ui = attachedFramework(); ui && m_add)
                    ui->set(m_add, ui->props().Visibility, isAddTabButtonVisible());
        });
}
TabView::~TabView()
{
    if (!isOwnerThread())
        std::terminate();
    clearHeaderResources();
}
std::string TabView::className() const
{
    return "Velora.Fluent.TabView";
}
const XamlTypeDeclaration& TabView::declaration()
{
    return pageDeclaration("Velora.Fluent.TabView");
}
std::string TabView::registerType(Framework& ui)
{
    return registerSelector(ui, declaration(), [] { return createElement<TabView>(); });
}
bool TabView::hasPivotProperties() const noexcept
{
    return false;
}
bool TabView::acceptsPage(const SelectorPage& page) const
{
    return page.header.empty() && static_cast<bool>(std::dynamic_pointer_cast<TabViewItem>(page.content));
}
std::string TabView::pageHeader(usize index) const
{
    return static_cast<TabViewItem&>(*pages()[index].content).header();
}
std::vector<std::shared_ptr<TabViewItem>> TabView::tabItems() const
{
    std::vector<std::shared_ptr<TabViewItem>> result;
    for (const auto& page : pages())
        result.push_back(std::static_pointer_cast<TabViewItem>(page.content));
    return result;
}
void TabView::setTabItems(std::vector<std::shared_ptr<TabViewItem>> items)
{
    if (!isOwnerThread())
        throw std::logic_error("TabView belongs to its UI thread");
    auto* ui = attachedFramework();
    const auto lifetime = ui ? ui->lifetime() : std::weak_ptr<void>{};
    const bool hadFocus = ui && ui->isWithin(ui->focused(), ui->tree().element(*this));
    const auto focused =
        hadFocus ? std::dynamic_pointer_cast<UIElement>(ui->tree().object(ui->focused())) : nullptr;
    const auto selected = selectedItem();
    std::vector<SelectorPage> replacement;
    std::optional<i32> next;
    for (usize index = 0; index < items.size(); ++index)
    {
        if (selected && items[index] == selected)
            next = static_cast<i32>(index);
        replacement.push_back({{}, std::move(items[index])});
    }
    if (selected && !next)
        next = replacement.empty() ? -1 : std::min(selectedIndex(), static_cast<i32>(replacement.size()) - 1);
    replacePages(std::move(replacement), next);
    if (hadFocus && !lifetime.expired() && attachedFramework() == ui)
    {
        const auto id = focused ? ui->tree().element(*focused) : 0;
        const auto selected = selectedIndex();
        ui->focus(id                                                                  ? id
                  : selected >= 0 && static_cast<usize>(selected) < m_captions.size() ? m_captions[selected]
                                                                                      : m_add);
    }
}
bool TabView::removeTab(const std::shared_ptr<TabViewItem>& item)
{
    auto items = tabItems();
    const auto found = std::ranges::find(items, item);
    if (found == items.end())
        return false;
    items.erase(found);
    setTabItems(std::move(items));
    return true;
}
bool TabView::isAddTabButtonVisible() const
{
    return getValue(property("IsAddTabButtonVisible")).asBool();
}
PropertySetResult TabView::setIsAddTabButtonVisible(bool value)
{
    return setValue(property("IsAddTabButtonVisible"), value);
}
bool TabView::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == property("IsAddTabButtonVisible").id)
        return value.isBool();
    return PageSelector::validatePropertyValue(p, value);
}
void TabView::clearHeaderResources()
{
    m_tabEvents.clear();
    for (const auto& [weak, token] : m_itemSubscriptions)
        if (const auto item = weak.lock())
            item->removePropertyChanged(token);
    m_itemSubscriptions.clear();
    m_captions.clear();
    m_closeButtons.clear();
    m_add = 0;
}
void TabView::onRebuildingHeaders()
{
    clearHeaderResources();
}
ElementId TabView::createHeader(Framework& ui, ElementId parent, usize)
{
    const auto group = ui.create(ElementKind::kGrid, parent);
    ui.set(group, ui.props().ColumnDefinitions,
           GridLengths{{GridLength::Unit::kStar, 1}, {GridLength::Unit::kAuto, 1}});
    const auto caption = ui.create(ElementKind::kButton, group);
    const auto close = ui.create(ElementKind::kButton, group);
    ui.set(close, ui.props().GridColumn, i64{1});
    ui.set(close, ui.props().Content, "\xEE\x9C\x91");
    ui.set(close, ui.props().FontFamily, "Segoe MDL2 Assets");
    ui.set(close, ui.props().FontSize, 12.0);
    ui.set(close, ui.props().Width, 28.0);
    ui.set(close, ui.props().IsTabStop, false);
    m_captions.push_back(caption);
    m_closeButtons.push_back(close);
    return caption;
}
void TabView::updateHeader(usize index, const std::shared_ptr<TabViewItem>& item)
{
    if (index >= pages().size() || pages()[index].content != item || index >= m_captions.size())
        return;
    auto* ui = attachedFramework();
    if (!ui)
        return;
    const auto caption = m_captions[index], close = m_closeButtons[index];
    const auto lifetime = ui->lifetime();
    ui->set(caption, ui->props().Content, item->header());
    if (!lifetime.expired() && attachedFramework() == ui)
        ui->set(close, ui->props().Visibility, item->isClosable());
}
void TabView::onHeadersRebuilt(Framework& ui, ElementId parent)
{
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(ui.tree().element(*this)));
    for (usize index = 0; index < pages().size(); ++index)
    {
        const auto item = std::static_pointer_cast<TabViewItem>(pages()[index].content);
        const std::weak_ptr<TabViewItem> weakItem = item;
        m_itemSubscriptions.emplace_back(
            item, item->addPropertyChanged(
                      [weak, weakItem, index](DependencyObject&, const PropertyDescriptor& p,
                                              const PropertyValue&, const PropertyValue&)
                      {
                          if (p.id != TabViewItem::headerProperty().id &&
                              p.id != TabViewItem::isClosableProperty().id)
                              return;
                          const auto owner = weak.lock();
                          const auto tab = weakItem.lock();
                          if (owner && tab)
                              static_cast<TabView&>(*owner).updateHeader(index, tab);
                      }));
        m_tabEvents.push_back(
            ui.subscribeEvent(m_closeButtons[index], ui.events().Click,
                              [weak, weakItem](ElementId, RoutedEventArgs&)
                              {
                                  const auto owner = weak.lock();
                                  const auto tab = weakItem.lock();
                                  if (!owner || !tab)
                                      return;
                                  auto& view = static_cast<TabView&>(*owner);
                                  const auto items = view.tabItems();
                                  if (const auto found = std::ranges::find(items, tab); found != items.end())
                                      view.requestClose(static_cast<i32>(found - items.begin()));
                              }));
        updateHeader(index, item);
        if (!attachedFramework())
            return;
    }
    m_add = ui.create(ElementKind::kButton, parent);
    ui.set(m_add, ui.props().Content, "\xEE\x9C\x90");
    ui.set(m_add, ui.props().FontFamily, "Segoe MDL2 Assets");
    ui.set(m_add, ui.props().FontSize, 16.0);
    ui.set(m_add, ui.props().Width, 32.0);
    ui.set(m_add, ui.props().Visibility, isAddTabButtonVisible());
    m_tabEvents.push_back(ui.subscribeEvent(m_add, ui.events().Click,
                                            [weak](ElementId, RoutedEventArgs&)
                                            {
                                                if (const auto owner = weak.lock())
                                                    static_cast<TabView&>(*owner).invokeAdd();
                                            }));
}
bool TabView::requestClose(i32 index)
{
    if (!isOwnerThread())
        throw std::logic_error("TabView belongs to its UI thread");
    if (!attachedFramework())
        return false;
    if (index < 0 || static_cast<usize>(index) >= pages().size())
        return false;
    const auto item = std::static_pointer_cast<TabViewItem>(pages()[index].content);
    if (!item->isClosable())
        return false;
    const auto keepAlive =
        attachedFramework() ? attachedFramework()->tree().object(attachedFramework()->tree().element(*this))
                            : nullptr;
    const TabCloseRequestedEventArgs args{item, index};
    const auto handlers = m_closeHandlers;
    for (const auto& [token, handler] : handlers)
        if (m_closeHandlers.contains(token))
            handler(args);
    item->raiseCloseRequested(args);
    return true; // Notification only. The application owns collection edits.
}
void TabView::invokeAdd()
{
    if (!attachedFramework() || !isAddTabButtonVisible())
        return;
    const auto keepAlive =
        attachedFramework() ? attachedFramework()->tree().object(attachedFramework()->tree().element(*this))
                            : nullptr;
    const auto handlers = m_addHandlers;
    for (const auto& [token, handler] : handlers)
        if (m_addHandlers.contains(token))
            handler();
}
void TabView::onDetached() noexcept
{
    clearHeaderResources();
    PageSelector::onDetached();
}
void TabView::onKeyDown(InputContext& context, KeyEventArgs& args)
{
    if (args.ctrl && !args.alt && args.key == Key::kF4)
    {
        args.handled = requestClose(selectedIndex());
        return;
    }
    PageSelector::onKeyDown(context, args);
}
u64 TabView::nextToken()
{
    if (!isOwnerThread())
        throw std::logic_error("TabView events belong to its UI thread");
    if (!m_nextToken)
        throw std::overflow_error("TabView event tokens exhausted");
    return m_nextToken++;
}
u64 TabView::addTabCloseRequested(std::function<void(const TabCloseRequestedEventArgs&)> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_closeHandlers.emplace(token, std::move(handler));
    return token;
}
u64 TabView::addAddTabButtonClick(std::function<void()> handler)
{
    if (!handler)
        return 0;
    const auto token = nextToken();
    m_addHandlers.emplace(token, std::move(handler));
    return token;
}
bool TabView::removeHandler(u64 token)
{
    return isOwnerThread() && (m_closeHandlers.erase(token) || m_addHandlers.erase(token));
}
} // namespace velora
