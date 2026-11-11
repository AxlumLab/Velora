// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// RadioButtons selection/column-major arrangement adapted from WinUI 2.3 RadioButtons/.
// ComboBox is a Velora implementation of the system control; theme source: porting/ComboBox/.
#include "FluentTypes.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/ComboBox.hpp>
#include <velora/fluent/RadioButtons.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
namespace
{
const PropertyDescriptor& choiceProperty(const XamlTypeDeclaration& declaration, std::string_view name)
{
    static PropertyRegistry registry;
    const auto* p = declaration.property(name);
    if (const auto* existing = registry.find(p->runtimeName))
        return *existing;
    const SchemaProperty schema{p->name, p->kind, {}, p->defaultValue, kSchemaPaint};
    return registry.define(p->runtimeName, std::get<PropertyValue>(parseValue(schema, p->defaultValue)),
                           PropertyFlags::kPaint);
}
struct Updating
{
    bool& flag;
    explicit Updating(bool& value) : flag(value)
    {
        flag = true;
    }
    ~Updating()
    {
        flag = false;
    }
};
} // namespace
const XamlTypeDeclaration& ChoiceControl::choiceDeclaration(std::string_view type)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find(type);
}
const PropertyDescriptor& ChoiceControl::property(std::string_view name) const
{
    return choiceProperty(choiceDeclaration(className()), name);
}
std::string ChoiceControl::registerChoice(Framework& ui, const XamlTypeDeclaration& d,
                                          SourceControlRegistry::Factory factory)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : d.properties)
        properties.push_back(&choiceProperty(d, p.name));
    return ui.registerSourceControl(d, std::move(factory), properties);
}
ChoiceControl::ChoiceControl(bool combo) : UIElement(ElementKind::kUserControl), m_combo(combo)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (!m_ui || m_updating)
                return;
            if (!m_combo && p.id == property("MaxColumns").id)
                rebuild();
            else
                updateParts();
            if (m_combo && p.id == property("IsDropDownOpen").id)
                syncDropDown();
        });
}
const PropertyDescriptor& ChoiceControl::selectedIndexProperty() const
{
    return property("SelectedIndex");
}
i32 ChoiceControl::selectedIndex() const
{
    return static_cast<i32>(getValue(selectedIndexProperty()).asInt());
}
PropertySetResult ChoiceControl::setSelectedIndex(i32 value)
{
    return setValue(selectedIndexProperty(), static_cast<i64>(value));
}
std::string ChoiceControl::selectedItem() const
{
    const auto index = selectedIndex();
    return index >= 0 && static_cast<usize>(index) < m_items.size() ? m_items[index] : std::string{};
}
std::string ChoiceControl::header() const
{
    return getValue(property("Header")).asString();
}
std::shared_ptr<UIElement> ChoiceControl::containerFromIndex(i32 index) const
{
    if (!isOwnerThread() || !m_ui || index < 0)
        return {};
    if (m_combo)
        return m_menu && m_menu->isOpen() && static_cast<usize>(index) < m_menu->items().size()
                   ? m_menu->items()[index]
                   : nullptr;
    return static_cast<usize>(index) < m_containers.size()
               ? std::dynamic_pointer_cast<UIElement>(m_ui->tree().object(m_containers[index]))
               : nullptr;
}
PropertySetResult ChoiceControl::setHeader(std::string value)
{
    return setValue(property("Header"), std::move(value));
}
const std::vector<std::string>& ChoiceControl::items() const noexcept
{
    return m_items;
}
void ChoiceControl::setItems(std::vector<std::string> values)
{
    if (!isOwnerThread() || m_updating || m_opening || (m_menu && m_menu->isOpen()))
        throw std::logic_error("Choice items require the UI thread and a closed, idle control");
    if (values.size() > static_cast<usize>(std::numeric_limits<i32>::max()))
        throw std::length_error("Too many choice items");
    for (const auto& value : values)
        utf8ToUtf16(value);
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    {
        Updating updating(m_updating);
        m_items = std::move(values);
        if (selectedIndex() >= static_cast<i32>(m_items.size()))
            setSelectedIndex(-1);
    }
    rebuild();
}
u64 ChoiceControl::addSelectionChanged(std::function<void(i32, i32)> handler)
{
    if (!handler)
        return 0;
    const auto id = selectedIndexProperty().id;
    return addPropertyChanged(
        [id, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                           const PropertyValue& before, const PropertyValue& after)
        {
            if (p.id == id)
                handler(static_cast<i32>(before.asInt()), static_cast<i32>(after.asInt()));
        });
}
bool ChoiceControl::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    for (const auto& d : choiceDeclaration(className()).properties)
    {
        if (d.id() != p.id)
            continue;
        if (d.kind == ValueKind::kInteger)
            return v.isInt() && v.asInt() >= (d.name == "MaxColumns" ? 1 : -1) &&
                   v.asInt() <= (d.name == "MaxColumns" ? 1000 : std::numeric_limits<i32>::max());
        if (d.kind == ValueKind::kBoolean)
            return v.isBool();
        if (!v.isString())
            return false;
        try
        {
            utf8ToUtf16(v.asString());
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return true;
}
void ChoiceControl::normalizePropertyValues(const PropertyDescriptor& p)
{
    // Retain an index assigned before items arrive (e.g. compiled XAML); resolve when items are set.
    if (p.id == selectedIndexProperty().id && !m_items.empty() &&
        selectedIndex() >= static_cast<i32>(m_items.size()))
        normalizeValue(selectedIndexProperty(), i64{-1});
}
void ChoiceControl::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded =
        loadComponent(ui,
                      *ResourceUri::parse(m_combo ? "ms-appx-framework:///Controls/ComboBoxView.vxbf"
                                                  : "ms-appx-framework:///Controls/RadioButtonsView.vxbf"),
                      options);
    if (!loaded.ok)
        throw std::runtime_error("Choice template: " + loaded.error);
    m_header = ui.tree().element(*loaded.names.at("HeaderText"));
    m_host = ui.tree().element(*loaded.names.at("ItemsHost"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {});
    rebuild();
    if (m_combo)
        syncDropDown();
}
void ChoiceControl::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_header = m_host = 0;
    m_containers.clear();
    m_events.clear();
    const auto menu = std::exchange(m_menu, {});
    if (menu)
        menu->hide();
    if (m_combo)
        try
        {
            setValue(property("IsDropDownOpen"), false);
        }
        catch (...)
        {
        }
}
void ChoiceControl::rebuild()
{
    if (!m_ui || !m_host || m_updating)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    {
        Updating updating(m_updating);
        m_events.clear();
        m_containers.clear();
        const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
        if (m_combo)
            m_events.push_back(m_ui->subscribeEvent(m_host, m_ui->events().Click,
                                                    [weak](ElementId, RoutedEventArgs&)
                                                    {
                                                        if (const auto owner = weak.lock())
                                                        {
                                                            auto& box = static_cast<ComboBox&>(*owner);
                                                            box.setIsDropDownOpen(!box.isDropDownOpen());
                                                        }
                                                    }));
        else
        {
            const auto old = m_ui->tree().children(m_host);
            for (const auto id : old)
                if (m_ui)
                    m_ui->tree().remove(id);
            if (!m_ui)
                return;
            const usize columns =
                std::min<usize>(static_cast<usize>(getValue(property("MaxColumns")).asInt()),
                                std::max<usize>(1, m_items.size()));
            const usize rows = std::max<usize>(1, (m_items.size() + columns - 1) / columns);
            m_ui->set(m_host, m_ui->props().ColumnDefinitions,
                      GridLengths(columns, {GridLength::Unit::kStar, 1}));
            m_ui->set(m_host, m_ui->props().RowDefinitions, GridLengths(rows, {GridLength::Unit::kAuto, 1}));
            usize column = 0, row = 0;
            for (usize index = 0; m_ui && index < m_items.size(); ++index)
            {
                const auto id = m_ui->create(ElementKind::kRadioButton, m_host);
                m_containers.push_back(id);
                m_ui->set(id, m_ui->props().Content, m_items[index]);
                m_ui->set(id, m_ui->props().GroupName, "Velora.RadioButtons." + std::to_string(m_element));
                m_ui->set(id, m_ui->props().GridColumn, static_cast<i64>(column));
                m_ui->set(id, m_ui->props().GridRow, static_cast<i64>(row));
                if (++row >= m_items.size() / columns + (column < m_items.size() % columns ? 1 : 0))
                {
                    ++column;
                    row = 0;
                }
                m_events.push_back(
                    m_ui->subscribeEvent(id, m_ui->events().Checked,
                                         [weak, index](ElementId, RoutedEventArgs&)
                                         {
                                             if (const auto owner = weak.lock())
                                             {
                                                 auto& choices = static_cast<ChoiceControl&>(*owner);
                                                 if (!choices.m_updating)
                                                     choices.setSelectedIndex(static_cast<i32>(index));
                                             }
                                         }));
            }
        }
    }
    updateParts();
}
void ChoiceControl::updateParts()
{
    if (!m_ui || !m_host || m_updating)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    Updating updating(m_updating);
    m_ui->set(m_header, m_ui->props().Text, header());
    if (!m_ui)
        return;
    m_ui->set(m_header, m_ui->props().Visibility, !header().empty());
    if (!m_ui)
        return;
    if (m_combo)
        m_ui->set(m_host, m_ui->props().Content,
                  selectedIndex() >= 0 && static_cast<usize>(selectedIndex()) < m_items.size()
                      ? selectedItem()
                      : getValue(property("PlaceholderText")).asString());
    else
        for (usize index = 0; m_ui && index < m_containers.size(); ++index)
        {
            m_ui->set(m_containers[index], m_ui->props().IsChecked,
                      static_cast<i32>(index) == selectedIndex());
            if (m_ui)
                m_ui->set(m_containers[index], m_ui->props().IsTabStop,
                          static_cast<i32>(index) == selectedIndex() || (selectedIndex() < 0 && index == 0));
        }
}
void ChoiceControl::syncDropDown()
{
    if (!m_ui || m_opening)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    if (!getValue(property("IsDropDownOpen")).asBool())
    {
        const auto menu = m_menu;
        if (menu)
            menu->hide();
        return;
    }
    if (m_menu && m_menu->isOpen())
        return;
    Updating opening(m_opening);
    const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
    m_menu = std::make_shared<MenuFlyout>();
    const auto menu = m_menu;
    for (usize index = 0; index < m_items.size(); ++index)
    {
        const auto item = createElement<MenuFlyoutItem>();
        item->setText(m_items[index]);
        item->setCommand(
            [weak, index]
            {
                if (const auto owner = weak.lock())
                    static_cast<ChoiceControl&>(*owner).setSelectedIndex(static_cast<i32>(index));
            });
        menu->append(item);
    }
    if (m_items.empty() || !menu->showAt(*m_ui, m_element,
                                         [weak](bool open)
                                         {
                                             if (const auto owner = weak.lock())
                                                 static_cast<ComboBox&>(*owner).setIsDropDownOpen(open);
                                         }))
        setValue(property("IsDropDownOpen"), false);
    if (m_ui && menu->isOpen() && selectedIndex() >= 0 &&
        static_cast<usize>(selectedIndex()) < menu->items().size())
        m_ui->focus(m_ui->tree().element(*menu->items()[selectedIndex()]));
}
void ChoiceControl::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (!m_ui)
        return;
    if (m_combo && (args.key == Key::kF4 || (args.alt && args.key == Key::kDown) || args.key == Key::kSpace ||
                    args.key == Key::kEnter))
    {
        setValue(property("IsDropDownOpen"), !getValue(property("IsDropDownOpen")).asBool());
        args.handled = true;
        return;
    }
    if (args.alt || m_items.empty())
        return;
    i32 index = selectedIndex();
    if (!m_combo)
        for (usize i = 0; i < m_containers.size(); ++i)
            if (m_ui->isWithin(m_ui->focused(), m_containers[i]))
                index = static_cast<i32>(i);
    if (!m_combo && index >= 0 && static_cast<usize>(index) < m_containers.size() &&
        (args.key == Key::kLeft || args.key == Key::kRight))
    {
        const auto& props = m_ui->props();
        const auto current = m_containers[index];
        const auto column =
            m_ui->tree().property(current, *props.GridColumn).asInt() + (args.key == Key::kRight ? 1 : -1);
        const auto row = m_ui->tree().property(current, *props.GridRow).asInt();
        for (usize i = 0; i < m_containers.size(); ++i)
        {
            const auto id = m_containers[i];
            if (m_ui->tree().property(id, *props.GridColumn).asInt() == column &&
                m_ui->tree().property(id, *props.GridRow).asInt() == row &&
                m_ui->tree().property(id, *props.IsEnabled).asBool() &&
                m_ui->tree().property(id, *props.Visibility).asBool())
            {
                if (!args.ctrl)
                    setSelectedIndex(static_cast<i32>(i));
                if (m_ui && i < m_containers.size())
                    m_ui->focus(m_containers[i]);
                break;
            }
        }
        args.handled = true;
        return;
    }
    if (args.key == Key::kHome)
        index = 0;
    else if (args.key == Key::kEnd)
        index = static_cast<i32>(m_items.size()) - 1;
    else if (args.key == Key::kDown || args.key == Key::kRight)
        ++index;
    else if (args.key == Key::kUp || args.key == Key::kLeft)
        --index;
    else
        return;
    index = std::clamp(index, 0, static_cast<i32>(m_items.size()) - 1);
    if (!args.ctrl || m_combo)
        setSelectedIndex(index);
    if (m_ui && !m_combo && static_cast<usize>(index) < m_containers.size())
        m_ui->focus(m_containers[index]);
    args.handled = true;
}
bool ChoiceControl::isFocusable() const noexcept
{
    return m_combo;
}
std::string ChoiceControl::automationName() const
{
    return header() + " " + selectedItem();
}
ComboBox::ComboBox() : ChoiceControl(true)
{
}
RadioButtons::RadioButtons() : ChoiceControl(false)
{
}
#define VELORA_CHOICE_TYPE(Type)                                                                             \
    std::string Type::className() const                                                                      \
    {                                                                                                        \
        return "Velora.Fluent." #Type;                                                                       \
    }                                                                                                        \
    const XamlTypeDeclaration& Type::declaration()                                                           \
    {                                                                                                        \
        return choiceDeclaration("Velora.Fluent." #Type);                                                    \
    }                                                                                                        \
    std::string Type::registerType(Framework& ui)                                                            \
    {                                                                                                        \
        return registerChoice(ui, declaration(), [] { return createElement<Type>(); });                      \
    }
VELORA_CHOICE_TYPE(ComboBox)
VELORA_CHOICE_TYPE(RadioButtons)
#undef VELORA_CHOICE_TYPE
bool ComboBox::isDropDownOpen() const
{
    return getValue(property("IsDropDownOpen")).asBool();
}
PropertySetResult ComboBox::setIsDropDownOpen(bool value)
{
    return setValue(property("IsDropDownOpen"), value);
}
std::string ComboBox::placeholderText() const
{
    return getValue(property("PlaceholderText")).asString();
}
PropertySetResult ComboBox::setPlaceholderText(std::string value)
{
    return setValue(property("PlaceholderText"), std::move(value));
}
i32 RadioButtons::maxColumns() const
{
    return static_cast<i32>(getValue(property("MaxColumns")).asInt());
}
PropertySetResult RadioButtons::setMaxColumns(i32 value)
{
    return setValue(property("MaxColumns"), static_cast<i64>(value));
}
} // namespace velora
