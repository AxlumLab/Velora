// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// System CalendarView/CalendarDatePicker behavior is supplied by Velora. Template references:
// porting/CalendarView/CalendarView_themeresources.xaml and CalendarDatePicker theme resources.
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/CalendarDatePicker.hpp>
namespace velora
{
namespace
{
constexpr std::string_view kView = "Velora.Fluent.CalendarView", kPicker = "Velora.Fluent.CalendarDatePicker";
std::optional<std::chrono::year_month_day> dateValue(const PropertyValue& value)
{
    return value.isString() ? parseCalendarDate(value.asString()) : std::nullopt;
}
} // namespace
CalendarView::CalendarView() : FluentElement(kView, "CalendarView")
{
}
const XamlTypeDeclaration& CalendarView::declaration()
{
    return fluentDeclaration(kView);
}
std::string CalendarView::registerType(Framework& ui)
{
    return registerFluentControl(ui, kView, [] { return createElement<CalendarView>(); });
}
const PropertyDescriptor& CalendarView::selectedDateProperty()
{
    return fluentProperty(kView, "SelectedDate");
}
std::optional<std::chrono::year_month_day> CalendarView::selectedDate() const
{
    return dateValue(getValue(selectedDateProperty()));
}
PropertySetResult CalendarView::setSelectedDate(std::optional<std::chrono::year_month_day> value)
{
    return setValue(selectedDateProperty(), value ? formatCalendarDate(*value) : "");
}
PropertySetResult CalendarView::setDisplayDate(std::chrono::year_month_day value)
{
    return setValue(property("DisplayDate"), formatCalendarDate(value));
}
std::chrono::year_month_day CalendarView::displayDate() const
{
    return dateValue(getValue(property("DisplayDate"))).value_or(calendarToday());
}
bool CalendarView::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == property("SelectedDate").id || p.id == property("DisplayDate").id)
        return value.isString() && (value.asString().empty() || parseCalendarDate(value.asString()));
    if (p.id == property("MinDate").id || p.id == property("MaxDate").id)
        return dateValue(value).has_value();
    if (p.id == property("SelectionMode").id)
        return value.isString() && (value.asString() == "None" || value.asString() == "Single");
    if (p.id == property("FirstDayOfWeek").id)
        return value.isInt() && value.asInt() >= 0 && value.asInt() <= 6;
    return FluentElement::validatePropertyValue(p, value);
}
void CalendarView::normalizePropertyValues(const PropertyDescriptor& p)
{
    const auto& minimum = property("MinDate");
    const auto& maximum = property("MaxDate");
    if (*dateValue(getValue(minimum)) > *dateValue(getValue(maximum)))
        normalizeValue(p.id == minimum.id ? maximum : minimum,
                       getValue(p.id == minimum.id ? minimum : maximum));
    if (getValue(property("SelectionMode")).asString() == "None")
        normalizeValue(selectedDateProperty(), "");
    else if (const auto selected = selectedDate())
        normalizeValue(selectedDateProperty(),
                       formatCalendarDate(std::clamp(*selected, *dateValue(getValue(minimum)),
                                                     *dateValue(getValue(maximum)))));
    if (const auto display = dateValue(getValue(property("DisplayDate"))))
        normalizeValue(property("DisplayDate"),
                       formatCalendarDate(std::clamp(*display, *dateValue(getValue(minimum)),
                                                     *dateValue(getValue(maximum)))));
}
void CalendarView::onAttached(Framework& ui, ElementId self)
{
    FluentElement::onAttached(ui, self);
    bindClick("PreviousButton",
              [](FluentElement& base) { static_cast<CalendarView&>(base).navigateMonths(-1); });
    bindClick("NextButton", [](FluentElement& base) { static_cast<CalendarView&>(base).navigateMonths(1); });
    for (usize i = 0; i < 42; ++i)
        bindClick("Day" + std::to_string(i),
                  [i](FluentElement& base) { static_cast<CalendarView&>(base).choose(i); });
    syncParts();
}
void CalendarView::navigateMonths(i32 delta)
{
    if (!isOwnerThread())
        throw std::logic_error("Calendar belongs to its UI thread");
    const auto current = displayDate();
    const auto minimum = *dateValue(getValue(property("MinDate"))),
               maximum = *dateValue(getValue(property("MaxDate")));
    const i64 index =
        std::clamp<i64>(static_cast<i64>(static_cast<i32>(current.year())) * 12 +
                            static_cast<i32>(static_cast<unsigned>(current.month())) - 1 + delta,
                        static_cast<i64>(static_cast<i32>(minimum.year())) * 12 +
                            static_cast<i32>(static_cast<unsigned>(minimum.month())) - 1,
                        static_cast<i64>(static_cast<i32>(maximum.year())) * 12 +
                            static_cast<i32>(static_cast<unsigned>(maximum.month())) - 1);
    setDisplayDate(
        std::clamp(std::chrono::year{static_cast<i32>(index / 12)} / static_cast<i32>(index % 12 + 1) / 1,
                   minimum, maximum));
}
void CalendarView::syncParts()
{
    if (!m_ui || !part("HeaderText") || m_syncing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    m_syncing = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_syncing};
    auto* ui = m_ui;
    const auto display = std::clamp(displayDate(), *dateValue(getValue(property("MinDate"))),
                                    *dateValue(getValue(property("MaxDate"))));
    const auto first = std::chrono::year_month_day{display.year() / display.month() / 1};
    const auto firstDay = std::chrono::sys_days{first};
    const auto weekday = static_cast<i32>(std::chrono::weekday{firstDay}.c_encoding());
    const auto weekStart = static_cast<i32>(getValue(property("FirstDayOfWeek")).asInt());
    const auto offset = (weekday - weekStart + 7) % 7;
    ui->set(part("HeaderText"), ui->props().Text, formatCalendarDate(first).substr(0, 7));
    constexpr std::array<std::string_view, 7> kDays{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (usize i = 0; i < 7 && m_ui == ui; ++i)
        ui->set(part("Weekday" + std::to_string(i)), ui->props().Text,
                std::string(kDays[(i + weekStart) % 7]));
    for (usize i = 0; i < 42 && m_ui == ui; ++i)
    {
        const std::chrono::year_month_day value{firstDay + std::chrono::days{static_cast<i32>(i) - offset}};
        const auto node = part("Day" + std::to_string(i));
        const bool inMonth = value.month() == first.month();
        const bool enabled = inMonth && value >= *dateValue(getValue(property("MinDate"))) &&
                             value <= *dateValue(getValue(property("MaxDate")));
        m_dates[i] = enabled ? std::optional{value} : std::nullopt;
        ui->set(node, ui->props().Content, inMonth ? std::to_string(static_cast<unsigned>(value.day())) : "");
        if (m_ui != ui)
            break;
        ui->set(node, ui->props().IsEnabled, enabled);
        if (m_ui != ui)
            break;
        ui->set(node, ui->props().Background, selectedDate() == value ? i64{0xFF0067C0} : i64{0xFFF3F3F3});
        if (m_ui != ui)
            break;
        ui->set(node, ui->props().Foreground, selectedDate() == value ? i64{0xFFFFFFFF} : i64{0xFF222222});
    }
}
void CalendarView::choose(usize index)
{
    if (index >= m_dates.size() || !m_dates[index] ||
        getValue(property("SelectionMode")).asString() == "None")
        return;
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    setSelectedDate(m_dates[index]);
    if (m_ui)
        m_ui->focus(m_element);
}
bool CalendarView::isFocusable() const noexcept
{
    return true;
}
void CalendarView::onKeyDown(InputContext&, KeyEventArgs& args)
{
    i32 delta = 0;
    switch (args.key)
    {
    case Key::kLeft:
        delta = -1;
        break;
    case Key::kRight:
        delta = 1;
        break;
    case Key::kUp:
        delta = -7;
        break;
    case Key::kDown:
        delta = 7;
        break;
    case Key::kPageUp:
        navigateMonths(-1);
        args.handled = true;
        return;
    case Key::kPageDown:
        navigateMonths(1);
        args.handled = true;
        return;
    default:
        return;
    }
    if (getValue(property("SelectionMode")).asString() == "None")
        return;
    const auto minimum = *dateValue(getValue(property("MinDate"))),
               maximum = *dateValue(getValue(property("MaxDate")));
    const auto current = std::clamp(selectedDate().value_or(displayDate()), minimum, maximum);
    const auto next =
        std::clamp(std::chrono::year_month_day{std::chrono::sys_days{current} + std::chrono::days{delta}},
                   minimum, maximum);
    args.handled = true;
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    setDisplayDate(next);
    setSelectedDate(next);
}
u64 CalendarView::addSelectedDatesChanged(std::function<void(std::optional<std::chrono::year_month_day>,
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
                handler(dateValue(before), dateValue(after));
        });
}
CalendarDatePicker::CalendarDatePicker() : FluentElement(kPicker, "DateTimePicker")
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if ((p.id == dateProperty().id || p.id == property("MinDate").id ||
                 p.id == property("MaxDate").id) &&
                m_flyout && m_flyout->isOpen())
                setIsCalendarOpen(false);
        });
}
const XamlTypeDeclaration& CalendarDatePicker::declaration()
{
    return fluentDeclaration(kPicker);
}
std::string CalendarDatePicker::registerType(Framework& ui)
{
    return registerFluentControl(ui, kPicker, [] { return createElement<CalendarDatePicker>(); });
}
const PropertyDescriptor& CalendarDatePicker::dateProperty()
{
    return fluentProperty(kPicker, "Date");
}
std::optional<std::chrono::year_month_day> CalendarDatePicker::date() const
{
    return dateValue(getValue(dateProperty()));
}
PropertySetResult CalendarDatePicker::setDate(std::optional<std::chrono::year_month_day> value)
{
    return setValue(dateProperty(), value ? formatCalendarDate(*value) : "");
}
bool CalendarDatePicker::isCalendarOpen() const
{
    return getValue(property("IsCalendarOpen")).asBool();
}
PropertySetResult CalendarDatePicker::setIsCalendarOpen(bool value)
{
    return setValue(property("IsCalendarOpen"), value);
}
bool CalendarDatePicker::isFocusable() const noexcept
{
    return true;
}
bool CalendarDatePicker::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == dateProperty().id)
        return value.isString() && (value.asString().empty() || dateValue(value));
    if (p.id == property("MinDate").id || p.id == property("MaxDate").id)
        return dateValue(value).has_value();
    return FluentElement::validatePropertyValue(p, value);
}
void CalendarDatePicker::normalizePropertyValues(const PropertyDescriptor& p)
{
    const auto& minimum = property("MinDate");
    const auto& maximum = property("MaxDate");
    if (*dateValue(getValue(minimum)) > *dateValue(getValue(maximum)))
        normalizeValue(p.id == minimum.id ? maximum : minimum,
                       getValue(p.id == minimum.id ? minimum : maximum));
    if (const auto selected = date())
        normalizeValue(dateProperty(), formatCalendarDate(std::clamp(*selected, *dateValue(getValue(minimum)),
                                                                     *dateValue(getValue(maximum)))));
}
void CalendarDatePicker::onAttached(Framework& ui, ElementId self)
{
    FluentElement::onAttached(ui, self);
    bindClick("FlyoutButton",
              [](FluentElement& base) { static_cast<CalendarDatePicker&>(base).setIsCalendarOpen(true); });
    syncParts();
}
void CalendarDatePicker::syncParts()
{
    if (!m_ui || !part("FlyoutButton") || m_syncing)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    m_syncing = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{m_syncing};
    auto* ui = m_ui;
    ui->set(part("HeaderContentPresenter"), ui->props().Text, getValue(property("Header")));
    if (m_ui != ui)
        return;
    ui->set(part("HeaderContentPresenter"), ui->props().Visibility,
            !getValue(property("Header")).asString().empty());
    if (m_ui != ui)
        return;
    ui->set(part("FlyoutButton"), ui->props().Content,
            date() ? formatCalendarDate(*date()) : getValue(property("PlaceholderText")).asString());
    if (m_ui != ui)
        return;
    if (!isCalendarOpen())
    {
        if (m_flyout)
            m_flyout->hide();
        return;
    }
    if (m_flyout && m_flyout->isOpen())
        return;
    const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
    if (!m_flyout)
        m_flyout = std::make_shared<Flyout>(
            [weak](Framework& host) -> ElementId
            {
                const auto owner = weak.lock();
                if (!owner)
                    throw std::runtime_error("Calendar picker was destroyed");
                auto& self = static_cast<CalendarDatePicker&>(*owner);
                const auto calendar = createElement<CalendarView>();
                calendar->setValue(fluentProperty(kView, "MinDate"), self.getValue(self.property("MinDate")));
                calendar->setValue(fluentProperty(kView, "MaxDate"), self.getValue(self.property("MaxDate")));
                calendar->setSelectedDate(self.date());
                calendar->setDisplayDate(self.date().value_or(calendarToday()));
                calendar->addSelectedDatesChanged(
                    [weak](auto, auto selected)
                    {
                        if (const auto object = weak.lock())
                        {
                            auto& picker = static_cast<CalendarDatePicker&>(*object);
                            picker.setDate(selected);
                            picker.setIsCalendarOpen(false);
                        }
                    });
                return host.insert(calendar);
            });
    const auto flyout = m_flyout;
    const bool opened =
        flyout->showAt(*ui, m_element,
                       [weak](bool open)
                       {
                           if (!open)
                               if (const auto owner = weak.lock())
                                   static_cast<CalendarDatePicker&>(*owner).setIsCalendarOpen(false);
                       });
    if (!opened)
        setIsCalendarOpen(false);
}
void CalendarDatePicker::onDetached() noexcept
{
    auto flyout = std::exchange(m_flyout, {});
    FluentElement::onDetached();
    if (flyout)
        flyout->hide();
    try
    {
        setIsCalendarOpen(false);
    }
    catch (...)
    {
    }
}
void CalendarDatePicker::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.key == Key::kF4 || args.key == Key::kEnter || args.key == Key::kSpace ||
        (args.alt && args.key == Key::kDown))
    {
        args.handled = true;
        setIsCalendarOpen(true);
    }
}
u64 CalendarDatePicker::addDateChanged(std::function<void(std::optional<std::chrono::year_month_day>,
                                                          std::optional<std::chrono::year_month_day>)>
                                           handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                       const PropertyValue& before, const PropertyValue& after)
        {
            if (p.id == dateProperty().id)
                handler(dateValue(before), dateValue(after));
        });
}
} // namespace velora
