// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/CalendarView.hpp>
#include <velora/fluent/Flyout.hpp>
namespace velora
{
class CalendarDatePicker final : public FluentElement
{
  public:
    CalendarDatePicker();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& dateProperty();
    std::optional<std::chrono::year_month_day> date() const;
    PropertySetResult setDate(std::optional<std::chrono::year_month_day>);
    bool isCalendarOpen() const;
    PropertySetResult setIsCalendarOpen(bool);
    u64 addDateChanged(std::function<void(std::optional<std::chrono::year_month_day>,
                                          std::optional<std::chrono::year_month_day>)>);
    void onAttached(Framework&, ElementId) override;
    void onDetached() noexcept override;
    void onKeyDown(InputContext&, KeyEventArgs&) override;
    bool isFocusable() const noexcept override;

  protected:
    void syncParts() override;
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;

  private:
    std::shared_ptr<Flyout> m_flyout;
    bool m_syncing = false;
};
} // namespace velora
