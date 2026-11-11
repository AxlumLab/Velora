// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 CommandBarFlyout.cpp and CommandBarFlyoutCommandBar.cpp.
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/CommandBarFlyout.hpp>

namespace velora
{
CommandBarFlyout::CommandBarFlyout() = default;
CommandBarFlyout::~CommandBarFlyout()
{
    if (!isOwnerThread())
        std::terminate();
    hide();
}
void CommandBarFlyout::requireEditable() const
{
    if (!isOwnerThread() || isOpen() || m_opening)
        throw std::logic_error("Close CommandBarFlyout before editing commands on its UI thread");
}
void CommandBarFlyout::append(std::shared_ptr<UIElement> item, bool secondary)
{
    requireEditable();
    if (!item || !item->isOwnerThread() || item->isInTree() ||
        (!std::dynamic_pointer_cast<AppBarButton>(item) &&
         !std::dynamic_pointer_cast<AppBarSeparator>(item)) ||
        std::ranges::find(m_primary, item) != m_primary.end() ||
        std::ranges::find(m_secondary, item) != m_secondary.end())
        throw std::invalid_argument("CommandBarFlyout requires distinct detached AppBar commands");
    (secondary ? m_secondary : m_primary).push_back(std::move(item));
}
void CommandBarFlyout::appendPrimary(std::shared_ptr<UIElement> item)
{
    append(std::move(item), false);
}
void CommandBarFlyout::appendSecondary(std::shared_ptr<UIElement> item)
{
    append(std::move(item), true);
}
void CommandBarFlyout::clearPrimary()
{
    requireEditable();
    m_primary.clear();
}
void CommandBarFlyout::clearSecondary()
{
    requireEditable();
    m_secondary.clear();
}
const std::vector<std::shared_ptr<UIElement>>& CommandBarFlyout::primaryCommands() const noexcept
{
    return m_primary;
}
const std::vector<std::shared_ptr<UIElement>>& CommandBarFlyout::secondaryCommands() const noexcept
{
    return m_secondary;
}
bool CommandBarFlyout::isOpen() const noexcept
{
    return m_flyout && m_flyout->isOpen();
}
const std::string& CommandBarFlyout::lastError() const noexcept
{
    static const std::string empty;
    return m_flyout ? m_flyout->lastError() : empty;
}
void CommandBarFlyout::closed()
{
    m_events.clear();
    m_ui = nullptr;
    m_lifetime.reset();
    m_root = m_primaryHost = m_secondaryHost = m_more = 0;
}
void CommandBarFlyout::hide()
{
    if (!isOwnerThread())
        throw std::logic_error("CommandBarFlyout belongs to its UI thread");
    const auto flyout = m_flyout;
    if (flyout)
        flyout->hide();
    closed();
}
bool CommandBarFlyout::showAt(Framework& ui, ElementId anchor)
{
    if (!isOwnerThread() || !ui.isOwnerThread() || weak_from_this().expired() || m_opening ||
        (m_primary.empty() && m_secondary.empty()))
        return false;
    const auto keepAlive = shared_from_this();
    if (m_flyout && m_flyout->isOpenAt(ui, anchor))
        return true;
    hide();
    m_opening = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_opening};
    const auto weak = weak_from_this();
    m_flyout = std::make_shared<Flyout>(
        [weak](Framework& host)
        {
            const auto owner = weak.lock();
            if (!owner)
                throw std::runtime_error("CommandBarFlyout owner expired");
            return owner->buildPresenter(host);
        });
    const bool opened = m_flyout->showAt(ui, anchor,
                                         [weak](bool open)
                                         {
                                             if (!open)
                                                 if (const auto owner = weak.lock())
                                                     owner->closed();
                                         });
    if (!opened)
        closed();
    return opened;
}
ElementId CommandBarFlyout::buildPresenter(Framework& ui)
{
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/CommandBarFlyoutView.vxbf"));
    if (!loaded.ok)
        throw std::runtime_error("CommandBarFlyout template: " + loaded.error);
    const auto root = ui.tree().element(*loaded.root);
    m_ui = &ui;
    m_lifetime = ui.lifetime();
    m_root = root;
    m_primaryHost = ui.tree().element(*loaded.names.at("PrimaryCommands"));
    m_secondaryHost = ui.tree().element(*loaded.names.at("SecondaryCommands"));
    m_more = ui.tree().element(*loaded.names.at("MoreButton"));
    const auto weak = weak_from_this();
    try
    {
        for (const bool secondary : {false, true})
            for (const auto& item : secondary ? m_secondary : m_primary)
            {
                if (item->isInTree())
                    throw std::runtime_error("CommandBarFlyout command is already attached");
                if (const auto button = std::dynamic_pointer_cast<AppBarButton>(item))
                    button->setOverflowMode(secondary);
                else
                    item->setValue(AppBarSeparator::orientationProperty(),
                                   secondary ? "Horizontal" : "Vertical", PropertySource::kTemplate);
                const auto id = ui.insert(item, secondary ? m_secondaryHost : m_primaryHost);
                if (!id)
                    throw std::runtime_error("CommandBarFlyout command failed to attach");
                if (secondary)
                    m_events.push_back(ui.subscribeEvent(
                        id, ui.events().Click,
                        [weak](ElementId sender, RoutedEventArgs&)
                        {
                            const auto owner = weak.lock();
                            if (!owner || !owner->m_ui || owner->m_lifetime.expired())
                                return;
                            const auto button =
                                std::dynamic_pointer_cast<AppBarButton>(owner->m_ui->tree().object(sender));
                            if (button && !button->flyout())
                                owner->hide();
                        }));
            }
        ui.set(m_more, ui.props().Visibility, !m_primary.empty() && !m_secondary.empty());
        m_events.push_back(ui.subscribeEvent(m_more, ui.events().Click,
                                             [weak](ElementId, RoutedEventArgs&)
                                             {
                                                 if (const auto owner = weak.lock())
                                                     owner->expand(!owner->m_expanded);
                                             }));
        m_events.push_back(ui.subscribeEvent(root, ui.events().KeyDown,
                                             [weak](ElementId, RoutedEventArgs& args)
                                             {
                                                 if (auto* key = dynamic_cast<KeyEventArgs*>(&args))
                                                     if (const auto owner = weak.lock())
                                                         owner->navigate(*key);
                                             }));
        expand(true); // Standard context-menu opening starts with secondary commands visible.
        return root;
    }
    catch (...)
    {
        ui.tree().remove(root);
        closed();
        throw;
    }
}
void CommandBarFlyout::expand(bool expanded)
{
    if (!m_ui || m_lifetime.expired())
        return;
    auto* ui = m_ui;
    const auto lifetime = m_lifetime;
    if (!expanded && ui->isWithin(ui->focused(), m_secondaryHost))
        ui->focus(m_more);
    if (lifetime.expired() || m_ui != ui)
        return;
    m_expanded = expanded;
    ui->set(m_secondaryHost, ui->props().Visibility, expanded && !m_secondary.empty());
}
void CommandBarFlyout::navigate(KeyEventArgs& key)
{
    if (!m_ui || m_lifetime.expired() || key.alt || key.ctrl)
        return;
    const bool vertical = key.key == Key::kUp || key.key == Key::kDown;
    const bool horizontal = key.key == Key::kLeft || key.key == Key::kRight;
    const bool edge = key.key == Key::kHome || key.key == Key::kEnd;
    if (!vertical && !horizontal && !edge)
        return;
    auto* ui = m_ui;
    const bool inSecondary = ui->isWithin(ui->focused(), m_secondaryHost);
    if (horizontal && inSecondary)
        return;
    if (vertical && !inSecondary)
        expand(true);
    if (!m_ui || m_lifetime.expired())
        return;
    std::vector<ElementId> stops;
    const bool useSecondary = inSecondary || vertical;
    for (const auto& item : useSecondary ? m_secondary : m_primary)
    {
        const auto id = ui->tree().element(*item);
        if (id && item->isFocusable() && item->getValue(*ui->props().IsEnabled).asBool() &&
            item->getValue(*ui->props().Visibility).asBool())
            stops.push_back(id);
    }
    if (!useSecondary && !m_secondary.empty() && !m_primary.empty())
        stops.push_back(m_more);
    if (stops.empty())
        return;
    auto found = std::ranges::find_if(stops, [ui](ElementId id) { return ui->isWithin(ui->focused(), id); });
    const bool forward = key.key == Key::kRight || key.key == Key::kDown || key.key == Key::kHome;
    usize index = forward ? 0 : stops.size() - 1;
    if (!edge && found != stops.end())
        index = (static_cast<usize>(found - stops.begin()) + (forward ? 1 : stops.size() - 1)) % stops.size();
    ui->focus(stops[index]);
    key.handled = true;
}
} // namespace velora
