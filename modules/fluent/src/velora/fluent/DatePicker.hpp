// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/DateTimePicker.hpp>
namespace velora
{
class DatePicker final : public DateTimePicker
{
  public:
    DatePicker();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& selectedDateProperty();
    std::optional<std::chrono::year_month_day> selectedDate() const;
    PropertySetResult setSelectedDate(std::optional<std::chrono::year_month_day>);
    i32 minYear() const;
    PropertySetResult setMinYear(i32);
    i32 maxYear() const;
    PropertySetResult setMaxYear(i32);
    bool yearVisible() const;
    PropertySetResult setYearVisible(bool);
    bool monthVisible() const;
    PropertySetResult setMonthVisible(bool);
    bool dayVisible() const;
    PropertySetResult setDayVisible(bool);
    // Token is removed with removePropertyChanged; includes C++, XAML and effective-value updates.
    u64 addSelectedDateChanged(std::function<void(std::optional<std::chrono::year_month_day>,
                                                  std::optional<std::chrono::year_month_day>)>);
};
} // namespace velora
