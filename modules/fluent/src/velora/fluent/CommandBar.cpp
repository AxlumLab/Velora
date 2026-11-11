// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// System CommandBar behavior implemented against Velora; resources derive from WinUI 2.3.
#include "FluentTypes.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
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
    return p.id != orientationProperty().id ||
           (v.isString() && (v.asString() == "Horizontal" || v.asString() == "Vertical"));
}
std::string AppBarSeparator::registerType(Framework& ui)
{
    const std::array properties = {&orientationProperty()};
    return ui.registerSourceControl(
        declaration(), [] { return createElement<AppBarSeparator>(); }, properties);
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

CommandBar::CommandBar() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
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
std::string CommandBar::registerType(Framework& ui)
{
    const std::array properties = {&isOpenProperty()};
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
    return p.id != isOpenProperty().id || v.isBool();
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
    list.push_back(command);
    try
    {
        if (m_ui && !secondary)
        {
            prepare(command, false);
            if (!m_ui->insert(command, m_primaryHost))
                throw std::runtime_error("Command failed to attach");
        }
        updateMore();
    }
    catch (...)
    {
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
    setIsOpen(false);
    Changing changing(m_mutating);
    auto old = std::exchange(secondary ? m_secondary : m_primary, {});
    for (const auto& item : old)
        if (m_ui)
            if (const auto id = m_ui->tree().element(*item))
                m_ui->tree().remove(id);
    updateMore();
}
void CommandBar::updateMore()
{
    if (m_ui && m_more)
        m_ui->set(m_more, m_ui->props().Visibility, !m_secondary.empty());
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
    m_more = ui.tree().element(*loaded.names.at("MoreButton"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root),
                     {{"PrimaryCommands", m_primaryHost}, {"MoreButton", m_more}});
    for (const auto& item : m_primary)
    {
        prepare(item, false);
        if (!ui.insert(item, m_primaryHost))
            throw std::runtime_error("Primary command failed to attach");
    }
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
        for (const auto& item : m_secondary)
        {
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
        else if (m_secondary.empty() || !overflow->showAt(*m_ui, m_more,
                                                          [weak](bool open)
                                                          {
                                                              if (const auto owner = weak.lock())
                                                              {
                                                                  auto& bar =
                                                                      static_cast<CommandBar&>(*owner);
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
    m_element = m_primaryHost = m_more = 0;
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
    for (const auto& item : m_primary)
    {
        const auto id = m_ui->tree().element(*item);
        if (id && item->isFocusable() && m_ui->tree().property(id, *m_ui->props().IsEnabled).asBool() &&
            m_ui->tree().property(id, *m_ui->props().Visibility).asBool())
            stops.push_back(id);
    }
    if (!m_secondary.empty())
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
