// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <array>
#include <velora/fluent/CalendarDate.hpp>
#include <velora/fluent/FluentElement.hpp>
namespace velora
{
class CalendarView final : public FluentElement
{
  public:
    CalendarView();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& selectedDateProperty();
    std::optional<std::chrono::year_month_day> selectedDate() const;
    PropertySetResult setSelectedDate(std::optional<std::chrono::year_month_day>);
    PropertySetResult setDisplayDate(std::chrono::year_month_day);
    void navigateMonths(i32 delta);
    u64 addSelectedDatesChanged(std::function<void(std::optional<std::chrono::year_month_day>,
                                                   std::optional<std::chrono::year_month_day>)>);
    void onAttached(Framework&, ElementId) override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;

  protected:
    void syncParts() override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;

  private:
    std::chrono::year_month_day displayDate() const;
    void choose(usize);
    std::array<std::optional<std::chrono::year_month_day>, 42> m_dates;
    bool m_syncing = false;
};
} // namespace velora
