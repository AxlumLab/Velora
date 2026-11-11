// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Presentation adapted from porting/DatePicker and porting/TimePicker theme resources.
// System picker behavior is implemented by Velora; it is not present in WinUI 2.3 sources.
#include "FluentTypes.hpp"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <exception>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/DatePicker.hpp>
#include <velora/fluent/TimePicker.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
namespace
{
std::optional<i32> number(std::string_view text)
{
    i32 value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return {};
    return value;
}
i32 daysInMonth(i32 year, i32 month)
{
    const std::chrono::year_month_day_last last{
        std::chrono::year{year},
        std::chrono::month_day_last{std::chrono::month{static_cast<unsigned>(month)}}};
    return static_cast<i32>(static_cast<unsigned>(last.day()));
}
std::tm localNow()
{
    const auto now = std::time(nullptr);
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &now);
#else
    localtime_r(&now, &result);
#endif
    return result;
}
} // namespace
const XamlTypeDeclaration& DateTimePicker::pickerDeclaration(bool time)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find(time ? "Velora.Fluent.TimePicker" : "Velora.Fluent.DatePicker");
}
const PropertyDescriptor& DateTimePicker::pickerProperty(bool time, std::string_view name)
{
    static PropertyRegistry registry;
    static const bool initialized = []
    {
        for (const bool time : {false, true})
            for (const auto& p : pickerDeclaration(time).properties)
            {
                const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
                registry.define(p.runtimeName, std::get<PropertyValue>(parseValue(schema, p.defaultValue)),
                                PropertyFlags::kPaint);
            }
        return true;
    }();
    (void)initialized;
    return *registry.find(pickerDeclaration(time).property(name)->runtimeName);
}
const PropertyDescriptor& DateTimePicker::property(std::string_view name) const
{
    return pickerProperty(m_time, name);
}
std::string DateTimePicker::registerPicker(Framework& ui, bool time, SourceControlRegistry::Factory factory)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : pickerDeclaration(time).properties)
        properties.push_back(&pickerProperty(time, p.name));
    return ui.registerSourceControl(pickerDeclaration(time), std::move(factory), properties);
}
DateTimePicker::DateTimePicker(bool time) : UIElement(ElementKind::kUserControl), m_time(time)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            // External effective-value changes discard an uncommitted draft, including range changes.
            if (p.name.starts_with(m_time ? "TimePicker." : "DatePicker."))
                cancelPicker();
            updateParts();
        });
}
DateTimePicker::~DateTimePicker()
{
    if (!isOwnerThread())
        std::terminate();
    if (m_flyout)
        m_flyout->hide();
}
std::string DateTimePicker::className() const
{
    return m_time ? "Velora.Fluent.TimePicker" : "Velora.Fluent.DatePicker";
}
std::optional<std::chrono::year_month_day> DateTimePicker::parseDate(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        return {};
    // ISO date fields contain digits only, not signed from_chars spellings.
    for (usize i = 0; i < text.size(); ++i)
        if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9'))
            return {};
    const auto year = number(text.substr(0, 4)), month = number(text.substr(5, 2)),
               day = number(text.substr(8, 2));
    if (!year || !month || !day || *year < 1601 || *year > 9999)
        return {};
    const std::chrono::year_month_day value{std::chrono::year{*year},
                                            std::chrono::month{static_cast<unsigned>(*month)},
                                            std::chrono::day{static_cast<unsigned>(*day)}};
    return value.ok() ? std::optional{value} : std::nullopt;
}
std::string DateTimePicker::formatDate(std::chrono::year_month_day value)
{
    if (!value.ok() || static_cast<i32>(value.year()) < 1601 || static_cast<i32>(value.year()) > 9999)
        throw std::invalid_argument("DatePicker requires a valid Gregorian date in 1601..9999");
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "%04d-%02u-%02u", static_cast<i32>(value.year()),
                  static_cast<unsigned>(value.month()), static_cast<unsigned>(value.day()));
    return text.data();
}
std::optional<std::chrono::minutes> DateTimePicker::parseTime(std::string_view text)
{
    const auto colon = text.find(':');
    if (colon == std::string_view::npos || colon < 1 || colon > 2 || text.size() - colon < 2 ||
        text.size() - colon > 3)
        return {};
    for (usize i = 0; i < text.size(); ++i)
        if (i != colon && (text[i] < '0' || text[i] > '9'))
            return {};
    const auto hour = number(text.substr(0, colon)), minute = number(text.substr(colon + 1));
    if (!hour || !minute || *hour > 23 || *minute > 59)
        return {};
    return std::chrono::minutes{*hour * 60 + *minute};
}
std::string DateTimePicker::formatTime(std::chrono::minutes value)
{
    if (value.count() < 0 || value.count() >= 1440)
        throw std::invalid_argument("TimePicker requires minutes in 00:00..23:59");
    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), "%02d:%02d", static_cast<i32>(value.count() / 60),
                  static_cast<i32>(value.count() % 60));
    return text.data();
}
bool DateTimePicker::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == elementProperties().props.Content->id)
        return value.isString() && value.asString().empty();
    for (const auto& d : pickerDeclaration(m_time).properties)
    {
        if (p.id != d.id())
            continue;
        if (d.kind == ValueKind::kBoolean)
            return value.isBool();
        if (d.kind == ValueKind::kInteger)
            return value.isInt() && value.asInt() >= (m_time ? 1 : 1601) &&
                   value.asInt() <= (m_time ? 59 : 9999);
        if (!value.isString())
            return false;
        const auto& text = value.asString();
        if (d.name == "SelectedDate")
            return text.empty() || parseDate(text).has_value();
        if (d.name == "SelectedTime")
            return text.empty() || parseTime(text).has_value();
        if (d.name == "ClockIdentifier")
            return text == "12HourClock" || text == "24HourClock";
        try
        {
            utf8ToUtf16(text);
        }
        catch (const std::exception&)
        {
            return false;
        }
        return true;
    }
    return true;
}
void DateTimePicker::normalizePropertyValues(const PropertyDescriptor& changed)
{
    if (m_time)
    {
        if (const auto selected = parseTime(getValue(property("SelectedTime")).asString()))
            normalizeValue(property("SelectedTime"), formatTime(*selected));
        return;
    }
    const auto& minimum = property("MinYear");
    const auto& maximum = property("MaxYear");
    if (getValue(minimum).asInt() > getValue(maximum).asInt())
    {
        if (changed.id == minimum.id)
            normalizeValue(maximum, getValue(minimum));
        else
            normalizeValue(minimum, getValue(maximum));
    }
    if (const auto selected = parseDate(getValue(property("SelectedDate")).asString()))
    {
        const i32 year =
            std::clamp(static_cast<i32>(selected->year()), static_cast<i32>(getValue(minimum).asInt()),
                       static_cast<i32>(getValue(maximum).asInt()));
        const auto month = static_cast<i32>(static_cast<unsigned>(selected->month()));
        const auto day =
            std::min(static_cast<i32>(static_cast<unsigned>(selected->day())), daysInMonth(year, month));
        normalizeValue(property("SelectedDate"), formatDate(std::chrono::year{year} / month / day));
    }
}
std::string DateTimePicker::header() const
{
    return getValue(property("Header")).asString();
}
PropertySetResult DateTimePicker::setHeader(std::string value)
{
    return setValue(property("Header"), std::move(value));
}
bool DateTimePicker::isFocusable() const noexcept
{
    return true;
}
bool DateTimePicker::isPickerOpen() const noexcept
{
    return m_flyout && m_flyout->isOpen();
}
const std::string& DateTimePicker::lastError() const noexcept
{
    static const std::string empty;
    return m_flyout ? m_flyout->lastError() : empty;
}
std::string DateTimePicker::automationName() const
{
    return header() + ": " + getValue(property(m_time ? "SelectedTime" : "SelectedDate")).asString();
}
bool DateTimePicker::canOpen() const
{
    if (!m_ui || !isOwnerThread())
        return false;
    for (auto current = m_element; current; current = m_ui->tree().parent(current))
        if (!m_ui->tree().contains(current) ||
            !m_ui->tree().property(current, *m_ui->props().IsEnabled).asBool() ||
            !m_ui->tree().property(current, *m_ui->props().Visibility).asBool())
            return false;
    return true;
}
void DateTimePicker::updateParts()
{
    if (!m_ui || !m_button)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    auto* ui = m_ui;
    ui->set(m_header, ui->props().Text, header());
    if (m_ui != ui)
        return;
    ui->set(m_header, ui->props().Visibility, !header().empty());
    if (m_ui != ui)
        return;
    auto text = getValue(property(m_time ? "SelectedTime" : "SelectedDate")).asString();
    if (m_time && text.empty())
        text = "Hour : Minute";
    else if (m_time && getValue(property("ClockIdentifier")).asString() == "12HourClock")
    {
        const auto minutes = parseTime(text)->count();
        const auto hour = minutes / 60;
        std::array<char, 16> display{};
        std::snprintf(display.data(), display.size(), "%02d:%02d %s",
                      static_cast<i32>(hour % 12 ? hour % 12 : 12), static_cast<i32>(minutes % 60),
                      hour >= 12 ? "PM" : "AM");
        text = display.data();
    }
    else if (!m_time)
    {
        std::string visible;
        for (const auto& [name, part] : {std::pair{"YearVisible", text.empty() ? "Year" : text.substr(0, 4)},
                                         {"MonthVisible", text.empty() ? "Month" : text.substr(5, 2)},
                                         {"DayVisible", text.empty() ? "Day" : text.substr(8, 2)}})
            if (getValue(property(name)).asBool())
            {
                if (!visible.empty())
                    visible += " / ";
                visible += part;
            }
        text = visible.empty() ? "Select date" : visible;
    }
    ui->set(m_button, ui->props().Content, text);
}
void DateTimePicker::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/DateTimePickerView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("Date/time picker: " + loaded.error);
    m_header = ui.tree().element(*loaded.names.at("HeaderContentPresenter"));
    m_button = ui.tree().element(*loaded.names.at("FlyoutButton"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {});
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(self));
    m_events.push_back(ui.subscribeEvent(m_button, ui.events().Click,
                                         [weak](ElementId, RoutedEventArgs&)
                                         {
                                             if (const auto owner = weak.lock())
                                                 static_cast<DateTimePicker&>(*owner).openPicker();
                                         }));
    updateParts();
}
void DateTimePicker::presenterClosed()
{
    m_presenterEvents.clear();
    m_presenter = 0;
    m_fields.fill(0);
    m_labels.fill(0);
    m_values.fill(0);
}
void DateTimePicker::onDetached() noexcept
{
    auto flyout = std::exchange(m_flyout, {});
    m_ui = nullptr;
    m_element = m_header = m_button = 0;
    m_events.clear();
    if (flyout)
        flyout->hide();
    presenterClosed();
}
void DateTimePicker::initializeDraft()
{
    const auto now = localNow();
    if (m_time)
    {
        const auto selected = parseTime(getValue(property("SelectedTime")).asString());
        const auto minutes = selected ? selected->count() : now.tm_hour * 60 + now.tm_min;
        const auto increment = static_cast<i32>(getValue(property("MinuteIncrement")).asInt());
        m_draft = {static_cast<i32>(minutes / 60), static_cast<i32>(minutes % 60) / increment * increment, 0};
    }
    else
    {
        const auto selected = parseDate(getValue(property("SelectedDate")).asString());
        m_draft = selected ? std::array<i32, 3>{static_cast<i32>(selected->year()),
                                                static_cast<i32>(static_cast<unsigned>(selected->month())),
                                                static_cast<i32>(static_cast<unsigned>(selected->day()))}
                           : std::array<i32, 3>{now.tm_year + 1900, now.tm_mon + 1, now.tm_mday};
        m_draft[0] = std::clamp(m_draft[0], static_cast<i32>(getValue(property("MinYear")).asInt()),
                                static_cast<i32>(getValue(property("MaxYear")).asInt()));
        m_draft[2] = std::min(m_draft[2], daysInMonth(m_draft[0], m_draft[1]));
    }
}
void DateTimePicker::stepField(usize field, i32 delta)
{
    if (!isPickerOpen() || field >= 3)
        return;
    if (m_time)
    {
        if (field == 0)
            m_draft[0] = (m_draft[0] + delta + 24) % 24;
        else if (field == 1)
        {
            const auto increment = static_cast<i32>(getValue(property("MinuteIncrement")).asInt());
            const auto count = 59 / increment + 1;
            m_draft[1] = (m_draft[1] / increment + delta + count) % count * increment;
        }
        else
            m_draft[0] = (m_draft[0] + 12) % 24;
    }
    else
    {
        if (field == 0)
            m_draft[0] =
                std::clamp(m_draft[0] + delta, static_cast<i32>(getValue(property("MinYear")).asInt()),
                           static_cast<i32>(getValue(property("MaxYear")).asInt()));
        else if (field == 1)
            m_draft[1] = (m_draft[1] - 1 + delta + 12) % 12 + 1;
        else
        {
            const auto count = daysInMonth(m_draft[0], m_draft[1]);
            m_draft[2] = (m_draft[2] - 1 + delta + count) % count + 1;
        }
        m_draft[2] = std::min(m_draft[2], daysInMonth(m_draft[0], m_draft[1]));
    }
    updateDraft();
}
void DateTimePicker::updateDraft()
{
    auto* ui = m_ui;
    if (!ui || !m_presenter)
        return;
    const auto keepAlive = ui->tree().object(m_element);
    for (usize i = 0; i < 3 && m_ui == ui && m_presenter; ++i)
    {
        const bool twelve = m_time && getValue(property("ClockIdentifier")).asString() == "12HourClock";
        const bool visible = m_time ? i != 2 || twelve
                                    : getValue(property(i == 0   ? "YearVisible"
                                                        : i == 1 ? "MonthVisible"
                                                                 : "DayVisible"))
                                          .asBool();
        ui->set(m_fields[i], ui->props().Visibility, visible);
        if (m_ui != ui || !m_presenter)
            break;
        ui->set(m_labels[i], ui->props().Text,
                m_time ? (i == 0   ? "Hour"
                          : i == 1 ? "Minute"
                                   : "Period")
                       : (i == 0   ? "Year"
                          : i == 1 ? "Month"
                                   : "Day"));
        if (m_ui != ui || !m_presenter)
            break;
        std::string text = std::to_string(m_draft[i]);
        if (m_time && i == 2)
            text = m_draft[0] >= 12 ? "PM" : "AM";
        else if (m_time && i == 0 && twelve)
            text = std::to_string(m_draft[0] % 12 ? m_draft[0] % 12 : 12);
        else if ((m_time || i != 0) && text.size() < 2)
            text = "0" + text;
        ui->set(m_values[i], ui->props().Text, text);
    }
}
ElementId DateTimePicker::buildPresenter(Framework& ui)
{
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/DateTimePickerFlyoutView.vxbf"));
    if (!loaded.ok)
        throw std::runtime_error("Picker presenter: " + loaded.error);
    const auto root = ui.tree().element(*loaded.root);
    const auto weak = std::weak_ptr<DependencyObject>(ui.tree().object(m_element));
    try
    {
        m_presenter = root;
        for (usize i = 0; i < 3; ++i)
        {
            const auto suffix = std::to_string(i);
            m_fields[i] = ui.tree().element(*loaded.names.at("Field" + suffix));
            m_labels[i] = ui.tree().element(*loaded.names.at("Label" + suffix));
            m_values[i] = ui.tree().element(*loaded.names.at("Value" + suffix));
            for (const auto& [name, delta] : {std::pair{"Increase", 1}, {"Decrease", -1}})
                m_presenterEvents.push_back(
                    ui.subscribeEvent(ui.tree().element(*loaded.names.at(name + suffix)), ui.events().Click,
                                      [weak, i, delta](ElementId, RoutedEventArgs&)
                                      {
                                          if (const auto owner = weak.lock())
                                              static_cast<DateTimePicker&>(*owner).stepField(i, delta);
                                      }));
        }
        m_presenterEvents.push_back(
            ui.subscribeEvent(ui.tree().element(*loaded.names.at("AcceptButton")), ui.events().Click,
                              [weak](ElementId, RoutedEventArgs&)
                              {
                                  if (const auto owner = weak.lock())
                                      static_cast<DateTimePicker&>(*owner).acceptPicker();
                              }));
        m_presenterEvents.push_back(
            ui.subscribeEvent(ui.tree().element(*loaded.names.at("DismissButton")), ui.events().Click,
                              [weak](ElementId, RoutedEventArgs&)
                              {
                                  if (const auto owner = weak.lock())
                                      static_cast<DateTimePicker&>(*owner).cancelPicker();
                              }));
        m_presenterEvents.push_back(ui.subscribeEvent(
            root, ui.events().KeyDown,
            [weak](ElementId, RoutedEventArgs& routed)
            {
                const auto owner = weak.lock();
                auto* args = dynamic_cast<KeyEventArgs*>(&routed);
                if (!owner || !args)
                    return;
                auto& self = static_cast<DateTimePicker&>(*owner);
                if (args->key != Key::kUp && args->key != Key::kDown)
                    return;
                for (usize i = 0; i < 3; ++i)
                    if (self.m_ui && self.m_ui->isWithin(args->originalSource, self.m_fields[i]))
                    {
                        args->handled = true;
                        self.stepField(i, args->key == Key::kUp ? 1 : -1);
                        return;
                    }
            }));
        updateDraft();
        return root;
    }
    catch (...)
    {
        ui.tree().remove(root);
        presenterClosed();
        throw;
    }
}
bool DateTimePicker::openPicker()
{
    if (isPickerOpen())
        return true;
    if (!canOpen() || m_opening)
        return false;
    const auto keepAlive = m_ui->tree().object(m_element);
    m_opening = true;
    struct Reset
    {
        bool& value;
        ~Reset()
        {
            value = false;
        }
    } reset{m_opening};
    const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
    if (!m_flyout)
        m_flyout = std::make_shared<Flyout>(
            [weak](Framework& ui) -> ElementId
            {
                if (const auto owner = weak.lock())
                    return static_cast<DateTimePicker&>(*owner).buildPresenter(ui);
                throw std::runtime_error("Picker was destroyed");
            });
    m_ui->focus(m_element);
    if (!canOpen())
        return false;
    initializeDraft();
    const auto flyout = m_flyout;
    const bool opened = flyout->showAt(*m_ui, m_element,
                                       [weak](bool open)
                                       {
                                           if (!open)
                                               if (const auto owner = weak.lock())
                                                   static_cast<DateTimePicker&>(*owner).presenterClosed();
                                       });
    if (!opened)
        presenterClosed();
    return opened;
}
void DateTimePicker::acceptPicker()
{
    if (!isOwnerThread())
        throw std::logic_error("Picker belongs to its UI thread");
    if (!isPickerOpen() || !m_ui)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto value = m_time ? formatTime(std::chrono::minutes{m_draft[0] * 60 + m_draft[1]})
                              : formatDate(std::chrono::year{m_draft[0]} / m_draft[1] / m_draft[2]);
    // Close the old transaction before publishing; a value handler may open another picker.
    cancelPicker();
    setValue(property(m_time ? "SelectedTime" : "SelectedDate"), value);
}
void DateTimePicker::cancelPicker()
{
    if (!isOwnerThread())
        throw std::logic_error("Picker belongs to its UI thread");
    const auto flyout = m_flyout;
    if (flyout)
        flyout->hide();
}
void DateTimePicker::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.key == Key::kF4 || (args.alt && args.key == Key::kDown) ||
        (args.originalSource == m_element && (args.key == Key::kEnter || args.key == Key::kSpace)))
    {
        args.handled = true;
        openPicker();
    }
}
DatePicker::DatePicker() : DateTimePicker(false)
{
}
TimePicker::TimePicker() : DateTimePicker(true)
{
}
const XamlTypeDeclaration& DatePicker::declaration()
{
    return pickerDeclaration(false);
}
const XamlTypeDeclaration& TimePicker::declaration()
{
    return pickerDeclaration(true);
}
std::string DatePicker::registerType(Framework& ui)
{
    return registerPicker(ui, false, [] { return createElement<DatePicker>(); });
}
std::string TimePicker::registerType(Framework& ui)
{
    return registerPicker(ui, true, [] { return createElement<TimePicker>(); });
}
const PropertyDescriptor& DatePicker::selectedDateProperty()
{
    return pickerProperty(false, "SelectedDate");
}
const PropertyDescriptor& TimePicker::selectedTimeProperty()
{
    return pickerProperty(true, "SelectedTime");
}
std::optional<std::chrono::year_month_day> DatePicker::selectedDate() const
{
    return parseDate(getValue(selectedDateProperty()).asString());
}
PropertySetResult DatePicker::setSelectedDate(std::optional<std::chrono::year_month_day> value)
{
    return setValue(selectedDateProperty(), value ? formatDate(*value) : "");
}
std::optional<std::chrono::minutes> TimePicker::selectedTime() const
{
    return parseTime(getValue(selectedTimeProperty()).asString());
}
PropertySetResult TimePicker::setSelectedTime(std::optional<std::chrono::minutes> value)
{
    return setValue(selectedTimeProperty(), value ? formatTime(*value) : "");
}
#define VELORA_DATE_FIELD(Type, Storage, getter, Setter, Name, Accessor)                                     \
    Type DatePicker::getter() const                                                                          \
    {                                                                                                        \
        return static_cast<Type>(getValue(property(Name)).Accessor());                                       \
    }                                                                                                        \
    PropertySetResult DatePicker::set##Setter(Type value)                                                    \
    {                                                                                                        \
        return setValue(property(Name), static_cast<Storage>(value));                                        \
    }
VELORA_DATE_FIELD(i32, i64, minYear, MinYear, "MinYear", asInt)
VELORA_DATE_FIELD(i32, i64, maxYear, MaxYear, "MaxYear", asInt)
VELORA_DATE_FIELD(bool, bool, yearVisible, YearVisible, "YearVisible", asBool)
VELORA_DATE_FIELD(bool, bool, monthVisible, MonthVisible, "MonthVisible", asBool)
VELORA_DATE_FIELD(bool, bool, dayVisible, DayVisible, "DayVisible", asBool)
#undef VELORA_DATE_FIELD
i32 TimePicker::minuteIncrement() const
{
    return static_cast<i32>(getValue(property("MinuteIncrement")).asInt());
}
PropertySetResult TimePicker::setMinuteIncrement(i32 value)
{
    return setValue(property("MinuteIncrement"), static_cast<i64>(value));
}
std::string TimePicker::clockIdentifier() const
{
    return getValue(property("ClockIdentifier")).asString();
}
PropertySetResult TimePicker::setClockIdentifier(std::string value)
{
    return setValue(property("ClockIdentifier"), std::move(value));
}
u64 DatePicker::addSelectedDateChanged(std::function<void(std::optional<std::chrono::year_month_day>,
                                                          std::optional<std::chrono::year_month_day>)>
                                           handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                       const PropertyValue& before, const PropertyValue& after)
        {
            if (p.id == selectedDateProperty().id)
                handler(parseDate(before.asString()), parseDate(after.asString()));
        });
}
u64 TimePicker::addSelectedTimeChanged(
    std::function<void(std::optional<std::chrono::minutes>, std::optional<std::chrono::minutes>)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                       const PropertyValue& before, const PropertyValue& after)
        {
            if (p.id == selectedTimeProperty().id)
                handler(parseTime(before.asString()), parseTime(after.asString()));
        });
}
} // namespace velora
