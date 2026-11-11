// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// System CommandBar behavior implemented against Velora; resources derive from WinUI 2.3.
#include "FluentTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/LayoutContext.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/CommandBar.hpp>
namespace velora
{
namespace
{
const XamlTypeDeclaration& commandType(std::string_view name)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find(name);
}
struct Changing
{
    bool& flag;
    explicit Changing(bool& value) : flag(value)
    {
        if (flag)
            throw std::logic_error("CommandBar collection is already changing");
        flag = true;
    }
    ~Changing()
    {
        flag = false;
    }
};
i32 commandOrder(const std::shared_ptr<UIElement>& item)
{
    if (const auto button = std::dynamic_pointer_cast<AppBarButton>(item))
        return button->dynamicOverflowOrder();
    return std::static_pointer_cast<AppBarSeparator>(item)->dynamicOverflowOrder();
}
} // namespace
AppBarSeparator::AppBarSeparator() : UIElement(ElementKind::kUserControl)
{
    setValue(*elementProperties().props.VerticalAlignment, "Center", PropertySource::kDefaultStyle);
    addPropertyChanged([this](DependencyObject&, const PropertyDescriptor&, const PropertyValue&,
                              const PropertyValue&) { updateLine(); });
}
std::string AppBarSeparator::className() const
{
    return "Velora.Fluent.AppBarSeparator";
}
const XamlTypeDeclaration& AppBarSeparator::declaration()
{
    return commandType("Velora.Fluent.AppBarSeparator");
}
const PropertyDescriptor& AppBarSeparator::orientationProperty()
{
    static PropertyRegistry registry;
    static const auto& property =
        registry.define("AppBarSeparator.Orientation", "Vertical", PropertyFlags::kPaint);
    return property;
}
std::string AppBarSeparator::orientation() const
{
    return getValue(orientationProperty()).asString();
}
PropertySetResult AppBarSeparator::setOrientation(std::string value)
{
    return setValue(orientationProperty(), std::move(value));
}
bool AppBarSeparator::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == dynamicOverflowOrderProperty().id)
        return v.isInt() && v.asInt() >= 0 && v.asInt() <= std::numeric_limits<i32>::max();
    return p.id != orientationProperty().id ||
           (v.isString() && (v.asString() == "Horizontal" || v.asString() == "Vertical"));
}
std::string AppBarSeparator::registerType(Framework& ui)
{
    const std::array properties = {&orientationProperty(), &dynamicOverflowOrderProperty()};
    return ui.registerSourceControl(
        declaration(), [] { return createElement<AppBarSeparator>(); }, properties);
}
const PropertyDescriptor& AppBarSeparator::dynamicOverflowOrderProperty()
{
    static PropertyRegistry registry;
    static const auto& property =
        registry.define("AppBarSeparator.DynamicOverflowOrder", i64{0}, PropertyFlags::kPaint);
    return property;
}
i32 AppBarSeparator::dynamicOverflowOrder() const
{
    return static_cast<i32>(getValue(dynamicOverflowOrderProperty()).asInt());
}
PropertySetResult AppBarSeparator::setDynamicOverflowOrder(i32 value)
{
    return setValue(dynamicOverflowOrderProperty(), static_cast<i64>(value));
}
void AppBarSeparator::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/AppBarSeparatorView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("AppBarSeparator template: " + loaded.error);
    m_line = ui.tree().element(*loaded.root);
    ui.applyTemplate(self, m_line, {});
    updateLine();
}
void AppBarSeparator::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_line = 0;
    try
    {
        clearValue(orientationProperty(), PropertySource::kTemplate);
    }
    catch (...)
    {
    }
}
void AppBarSeparator::updateLine()
{
    if (!m_ui || !m_line)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto& p = elementProperties().props;
    const bool horizontal = orientation() == "Horizontal";
    const auto set = [this](const PropertyDescriptor* p, PropertyValue value)
    {
        if (m_ui)
            m_ui->set(m_line, p, std::move(value));
    };
    set(p.Width, horizontal ? std::numeric_limits<f64>::quiet_NaN() : 1.0);
    set(p.Height, horizontal ? 1.0 : 32.0);
    set(p.MinWidth, horizontal ? 160.0 : 0.0);
    set(p.Margin, horizontal ? PropertyValue(Thickness{4, 8, 4, 8}) : PropertyValue(Thickness{8, 0, 8, 0}));
}

struct CommandBar::CommandSubscription
{
    std::weak_ptr<UIElement> item;
    u64 token = 0;
    ~CommandSubscription()
    {
        if (const auto owner = item.lock())
            owner->removePropertyChanged(token);
    }
};
CommandBar::~CommandBar() = default;
void CommandBar::observeCommand(const std::shared_ptr<UIElement>& command)
{
    const auto weak = std::weak_ptr<DependencyObject>(m_ui->tree().object(m_element));
    auto subscription = std::make_unique<CommandSubscription>();
    subscription->item = command;
    const auto id = std::dynamic_pointer_cast<AppBarToggleButton>(command)
                        ? AppBarToggleButton::dynamicOverflowOrderProperty().id
                        : (std::dynamic_pointer_cast<AppBarButton>(command)
                               ? AppBarButton::dynamicOverflowOrderProperty().id
                               : AppBarSeparator::dynamicOverflowOrderProperty().id);
    subscription->token = command->addPropertyChanged(
        [weak, id](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.id != id)
                return;
            if (const auto owner = weak.lock())
            {
                auto& bar = static_cast<CommandBar&>(*owner);
                bar.setIsOpen(false);
                if (bar.m_ui)
                    bar.m_ui->tree().invalidateMeasure(bar.m_element);
            }
        });
    m_commandSubscriptions.push_back(std::move(subscription));
}
u64 CommandBar::addDynamicOverflowItemsChanging(DynamicOverflowHandler handler)
{
    if (!isOwnerThread())
        throw std::logic_error("CommandBar events belong to the UI thread");
    if (!handler)
        return 0;
    if (!m_nextOverflowHandler)
        throw std::overflow_error("CommandBar event tokens exhausted");
    const auto token = m_nextOverflowHandler++;
    m_overflowHandlers.emplace(token, std::move(handler));
    return token;
}
bool CommandBar::removeDynamicOverflowItemsChanging(u64 token)
{
    return isOwnerThread() && m_overflowHandlers.erase(token) != 0;
}
void CommandBar::notifyOverflowChanges(const std::vector<bool>& next)
{
    Changing notifying(m_notifyingOverflow);
    const auto element = m_element;
    const auto handlers = m_overflowHandlers;
    for (const bool adding : {false, true})
    {
        if (!m_ui || m_element != element)
            return;
        DynamicOverflowItemsChangingEventArgs args{
            adding ? CommandBarDynamicOverflowAction::kAddingToOverflow
                   : CommandBarDynamicOverflowAction::kRemovingFromOverflow,
            {}};
        for (usize index = 0; index < next.size(); ++index)
            if (next[index] != m_primaryOverflow[index] && next[index] == adding)
                args.items.push_back(m_primary[index]);
        if (args.items.empty())
            continue;
        for (const auto& [token, handler] : handlers)
        {
            if (!m_ui || m_element != element)
                return;
            if (m_overflowHandlers.contains(token))
                handler(*this, args);
        }
    }
}
CommandBar::CommandBar() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (m_ui && (p.id == isOpenProperty().id || p.id == isDynamicOverflowEnabledProperty().id))
                m_ui->tree().invalidateMeasure(m_element);
            if (p.id == isOpenProperty().id)
                syncOpen();
        });
}
std::string CommandBar::className() const
{
    return "Velora.Fluent.CommandBar";
}
const XamlTypeDeclaration& CommandBar::declaration()
{
    return commandType("Velora.Fluent.CommandBar");
}
const PropertyDescriptor& CommandBar::isOpenProperty()
{
    static PropertyRegistry registry;
    static const auto& property = registry.define("CommandBar.IsOpen", false, PropertyFlags::kPaint);
    return property;
}
const PropertyDescriptor& CommandBar::isDynamicOverflowEnabledProperty()
{
    static PropertyRegistry registry;
    static const auto& property =
        registry.define("CommandBar.IsDynamicOverflowEnabled", true, PropertyFlags::kPaint);
    return property;
}
bool CommandBar::isDynamicOverflowEnabled() const
{
    return getValue(isDynamicOverflowEnabledProperty()).asBool();
}
PropertySetResult CommandBar::setIsDynamicOverflowEnabled(bool value)
{
    return setValue(isDynamicOverflowEnabledProperty(), value);
}
std::string CommandBar::registerType(Framework& ui)
{
    const std::array properties = {&isOpenProperty(), &isDynamicOverflowEnabledProperty()};
    return ui.registerSourceControl(declaration(), [] { return createElement<CommandBar>(); }, properties);
}
bool CommandBar::isOpen() const
{
    return getValue(isOpenProperty()).asBool();
}
PropertySetResult CommandBar::setIsOpen(bool value)
{
    return setValue(isOpenProperty(), value);
}
bool CommandBar::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == isOpenProperty().id && m_notifyingOverflow && v.isBool() && v.asBool())
        return false;
    return (p.id != isOpenProperty().id && p.id != isDynamicOverflowEnabledProperty().id) || v.isBool();
}
Size CommandBar::measureOverride(LayoutContext& context, ElementId self, const MeasureInput& input)
{
    if (!m_ui || !m_storage || m_mutating)
        return UIElement::measureOverride(context, self, input);
    const auto keepAlive = m_ui->tree().object(self);
    // Resizing dismisses the popup before its original commands return to the bar.
    if (isOpen() &&
        (input.content.width != m_lastWidth || isDynamicOverflowEnabled() != m_layoutDynamicOverflowEnabled))
        setIsOpen(false);
    if (isOpen())
        return UIElement::measureOverride(context, self, input);
    Changing changing(m_mutating);
    m_lastWidth = input.content.width;
    m_layoutDynamicOverflowEnabled = isDynamicOverflowEnabled();
    std::vector<float> widths;
    float total = 0;
    for (const auto& item : m_primary)
    {
        auto id = m_ui->tree().element(*item);
        if (!id)
        {
            prepare(item, false);
            id = m_ui->insert(item, m_storage);
            if (!id)
                throw std::runtime_error("Primary command failed to return from overflow");
        }
        const auto size = context.measure(id, {std::numeric_limits<float>::infinity(), input.content.height});
        widths.push_back(size.width);
        total += size.width;
    }
    const float moreWidth = static_cast<float>(context.number(m_more, context.props().Width));
    std::vector<bool> next(m_primary.size(), false);
    if (isDynamicOverflowEnabled() && std::isfinite(input.content.width) &&
        total + (m_secondary.empty() ? 0 : moreWidth) > input.content.width)
    {
        const float room = std::max(0.f, input.content.width - moreWidth);
        std::map<i32, std::vector<usize>> groups;
        for (usize index = 0; index < m_primary.size(); ++index)
            groups[commandOrder(m_primary[index])].push_back(index);
        // Positive priorities overflow in ascending groups. Zero retains the default end-first order.
        for (const auto& [order, indices] : groups)
        {
            if (!order || total <= room)
                continue;
            for (const auto index : indices)
            {
                next[index] = true;
                total -= widths[index];
            }
        }
        if (const auto defaults = groups.find(0); defaults != groups.end())
            for (auto it = defaults->second.rbegin(); it != defaults->second.rend() && total > room; ++it)
            {
                next[*it] = true;
                total -= widths[*it];
            }
    }
    notifyOverflowChanges(next);
    if (!m_ui || m_element != self)
        return {};
    m_primaryOverflow = std::move(next);
    updateMore();
    usize visibleIndex = 0;
    for (usize index = 0; index < m_primary.size(); ++index)
    {
        const auto& item = m_primary[index];
        const auto id = m_ui->tree().element(*item);
        const bool overflow = m_primaryOverflow[index];
        const auto host = overflow ? m_storage : m_primaryHost;
        const bool hadFocus = m_ui->isWithin(m_ui->focused(), id);
        if (m_ui->tree().parent(id) != host &&
            !m_ui->tree().reparent(host, id, m_ui->tree().children(host).size()))
            throw std::runtime_error("Command could not change its layout container");
        if (!overflow)
        {
            if (m_ui->tree().children(host).at(visibleIndex) != id)
                m_ui->tree().reorder(id, visibleIndex);
            ++visibleIndex;
        }
        if (const auto button = std::dynamic_pointer_cast<AppBarButton>(item))
            button->setOverflowMode(overflow);
        if (overflow && hadFocus)
            m_ui->focus(m_more);
    }
    updateMore();
    return UIElement::measureOverride(context, self, input);
}
const std::vector<std::shared_ptr<UIElement>>& CommandBar::primaryCommands() const noexcept
{
    return m_primary;
}
const std::vector<std::shared_ptr<UIElement>>& CommandBar::secondaryCommands() const noexcept
{
    return m_secondary;
}
void CommandBar::prepare(const std::shared_ptr<UIElement>& command, bool overflow)
{
    if (const auto button = std::dynamic_pointer_cast<AppBarButton>(command))
        button->setOverflowMode(overflow);
    else if (const auto separator = std::dynamic_pointer_cast<AppBarSeparator>(command))
        separator->setValue(AppBarSeparator::orientationProperty(), overflow ? "Horizontal" : "Vertical",
                            PropertySource::kTemplate);
}
void CommandBar::appendPrimary(std::shared_ptr<UIElement> item)
{
    append(std::move(item), false);
}
void CommandBar::appendSecondary(std::shared_ptr<UIElement> item)
{
    append(std::move(item), true);
}
void CommandBar::append(std::shared_ptr<UIElement> command, bool secondary)
{
    if (!isOwnerThread() || !command || !command->isOwnerThread() || command->isInTree() || isOpen() ||
        m_syncing ||
        (!std::dynamic_pointer_cast<AppBarButton>(command) &&
         !std::dynamic_pointer_cast<AppBarSeparator>(command)) ||
        std::ranges::find(m_primary, command) != m_primary.end() ||
        std::ranges::find(m_secondary, command) != m_secondary.end())
        throw std::invalid_argument(
            "CommandBar requires distinct detached commands; close overflow before editing");
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    Changing changing(m_mutating);
    auto& list = secondary ? m_secondary : m_primary;
    const auto subscriptionCount = m_commandSubscriptions.size();
    const auto layoutCount = m_primaryOverflow.size();
    list.push_back(command);
    try
    {
        if (m_ui && !secondary)
        {
            prepare(command, false);
            if (!m_ui->insert(command, m_primaryHost))
                throw std::runtime_error("Command failed to attach");
            observeCommand(command);
            m_primaryOverflow.push_back(false);
        }
        updateMore();
    }
    catch (...)
    {
        m_commandSubscriptions.resize(subscriptionCount);
        m_primaryOverflow.resize(layoutCount);
        if (m_ui)
            if (const auto id = m_ui->tree().element(*command);
                id && m_ui->tree().parent(id) == m_primaryHost)
                m_ui->tree().remove(id);
        list.pop_back();
        throw;
    }
}
void CommandBar::clearPrimary()
{
    clear(false);
}
void CommandBar::clearSecondary()
{
    clear(true);
}
void CommandBar::clear(bool secondary)
{
    if (!isOwnerThread() || m_syncing)
        throw std::logic_error("CommandBar collection is busy or belongs to another thread");
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    Changing changing(m_mutating);
    setIsOpen(false);
    auto old = std::exchange(secondary ? m_secondary : m_primary, {});
    for (const auto& item : old)
        if (m_ui)
            if (const auto id = m_ui->tree().element(*item))
                m_ui->tree().remove(id);
    if (!secondary)
    {
        m_primaryOverflow.clear();
        m_commandSubscriptions.clear();
    }
    updateMore();
}
void CommandBar::updateMore()
{
    if (m_ui && m_more)
        m_ui->set(m_more, m_ui->props().Visibility,
                  !m_secondary.empty() ||
                      std::ranges::any_of(m_primaryOverflow, [](bool value) { return value; }));
}
void CommandBar::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    Changing changing(m_mutating);
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/CommandBarView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("CommandBar template: " + loaded.error);
    m_primaryHost = ui.tree().element(*loaded.names.at("PrimaryCommands"));
    m_storage = ui.tree().element(*loaded.names.at("OverflowStorage"));
    m_more = ui.tree().element(*loaded.names.at("MoreButton"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root),
                     {{"PrimaryCommands", m_primaryHost}, {"MoreButton", m_more}});
    for (const auto& item : m_primary)
    {
        prepare(item, false);
        if (!ui.insert(item, m_primaryHost))
            throw std::runtime_error("Primary command failed to attach");
        observeCommand(item);
    }
    m_primaryOverflow.assign(m_primary.size(), false);
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    m_overflow = std::make_shared<Flyout>(
        [weak](Framework& host)
        {
            const auto owner = weak.lock();
            if (!owner)
                throw std::runtime_error("CommandBar was destroyed");
            return static_cast<CommandBar&>(*owner).buildOverflow(host);
        });
    m_events.push_back(ui.subscribeEvent(m_more, ui.events().Click,
                                         [weak](ElementId, RoutedEventArgs&)
                                         {
                                             if (const auto owner = weak.lock())
                                             {
                                                 auto& bar = static_cast<CommandBar&>(*owner);
                                                 bar.setIsOpen(!bar.isOpen());
                                             }
                                         }));
    m_events.push_back(ui.subscribeEvent(m_more, ui.events().KeyDown,
                                         [weak](ElementId, RoutedEventArgs& event)
                                         {
                                             auto* key = dynamic_cast<KeyEventArgs*>(&event);
                                             if (!key || (key->key != Key::kF4 && key->key != Key::kDown &&
                                                          key->key != Key::kUp))
                                                 return;
                                             if (const auto owner = weak.lock())
                                             {
                                                 auto& bar = static_cast<CommandBar&>(*owner);
                                                 bar.setIsOpen(true);
                                                 if (bar.m_ui && bar.isOpen() && key->key == Key::kUp)
                                                     bar.m_ui->moveFocus(false);
                                                 event.handled = true;
                                             }
                                         }));
    updateMore();
    syncOpen();
}
ElementId CommandBar::buildOverflow(Framework& ui)
{
    m_overflowEvents.clear();
    const auto panel = ui.create(ElementKind::kStackPanel);
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(m_element));
    try
    {
        ui.set(panel, ui.props().MinWidth, 200.0);
        std::vector<std::shared_ptr<UIElement>> commands;
        for (usize index = 0; index < m_primary.size(); ++index)
            if (m_primaryOverflow[index])
                commands.push_back(m_primary[index]);
        commands.insert(commands.end(), m_secondary.begin(), m_secondary.end());
        for (const auto& item : commands)
        {
            const auto existing = ui.tree().element(*item);
            if (existing && ui.tree().parent(existing) == m_storage)
                ui.tree().remove(existing);
            if (item->isInTree())
                throw std::runtime_error("Secondary command is already attached");
            prepare(item, true);
            const auto id = ui.insert(item, panel);
            if (!id)
                throw std::runtime_error("Secondary command failed to attach");
            m_overflowEvents.push_back(ui.subscribeEvent(id, ui.events().Click,
                                                         [weak](ElementId, RoutedEventArgs&)
                                                         {
                                                             if (const auto owner = weak.lock())
                                                                 static_cast<CommandBar&>(*owner).setIsOpen(
                                                                     false);
                                                         }));
        }
        m_overflowEvents.push_back(ui.subscribeEvent(
            panel, ui.events().KeyDown,
            [weak](ElementId, RoutedEventArgs& event)
            {
                auto* key = dynamic_cast<KeyEventArgs*>(&event);
                if (!key || (key->key != Key::kUp && key->key != Key::kDown && key->key != Key::kHome &&
                             key->key != Key::kEnd))
                    return;
                if (const auto owner = weak.lock())
                {
                    auto& bar = static_cast<CommandBar&>(*owner);
                    if (!bar.m_ui)
                        return;
                    if (key->key == Key::kHome || key->key == Key::kEnd)
                        bar.m_ui->focus(0);
                    if (bar.m_ui)
                        bar.m_ui->moveFocus(key->key == Key::kDown || key->key == Key::kHome);
                    event.handled = true;
                }
            }));
        return panel;
    }
    catch (...)
    {
        ui.tree().remove(panel);
        m_overflowEvents.clear();
        throw;
    }
}
void CommandBar::syncOpen()
{
    if (!m_ui || !m_overflow || m_syncing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    Changing syncing(m_syncing);
    const auto overflow = m_overflow;
    const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
    for (int pass = 0; pass < 4 && m_ui && m_overflow == overflow; ++pass)
    {
        if (isOpen() == overflow->isOpen())
            break;
        if (!isOpen())
        {
            overflow->hide();
            m_overflowEvents.clear();
        }
        else if ((m_secondary.empty() &&
                  std::ranges::none_of(m_primaryOverflow, [](bool value) { return value; })) ||
                 !overflow->showAt(*m_ui, m_more,
                                   [weak](bool open)
                                   {
                                       if (const auto owner = weak.lock())
                                       {
                                           auto& bar = static_cast<CommandBar&>(*owner);
                                           bar.setIsOpen(open);
                                           if (!open)
                                               bar.m_overflowEvents.clear();
                                       }
                                   }))
            setIsOpen(false);
    }
}
void CommandBar::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_primaryHost = m_storage = m_more = 0;
    m_primaryOverflow.clear();
    m_commandSubscriptions.clear();
    m_lastWidth = -1;
    m_events.clear();
    m_overflowEvents.clear();
    const auto overflow = std::exchange(m_overflow, {});
    if (overflow)
        overflow->hide();
    try
    {
        setIsOpen(false);
    }
    catch (...)
    {
    }
}
void CommandBar::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (!m_ui || args.ctrl || args.alt ||
        (args.key != Key::kLeft && args.key != Key::kRight && args.key != Key::kHome &&
         args.key != Key::kEnd))
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    std::vector<ElementId> stops;
    for (usize index = 0; index < m_primary.size(); ++index)
    {
        if (m_primaryOverflow[index])
            continue;
        const auto& item = m_primary[index];
        const auto id = m_ui->tree().element(*item);
        if (id && item->isFocusable() && m_ui->tree().property(id, *m_ui->props().IsEnabled).asBool() &&
            m_ui->tree().property(id, *m_ui->props().Visibility).asBool())
            stops.push_back(id);
    }
    if (!m_secondary.empty() || std::ranges::any_of(m_primaryOverflow, [](bool value) { return value; }))
        stops.push_back(m_more);
    if (stops.empty())
        return;
    const auto found =
        std::ranges::find_if(stops, [&](ElementId id) { return m_ui->isWithin(m_ui->focused(), id); });
    usize index = found == stops.end() ? 0 : static_cast<usize>(found - stops.begin());
    if (args.key == Key::kHome)
        index = 0;
    else if (args.key == Key::kEnd)
        index = stops.size() - 1;
    else
        index =
            args.key == Key::kRight ? (index + 1) % stops.size() : (index + stops.size() - 1) % stops.size();
    m_ui->focus(stops[index]);
    args.handled = true;
}
std::optional<AutomationControlType> CommandBar::automationControlType() const noexcept
{
    return AutomationControlType::kGroup;
}
} // namespace velora
