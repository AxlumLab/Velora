// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <velora/fluent/DateTimePicker.hpp>
namespace velora
{
class TimePicker final : public DateTimePicker
{
  public:
    TimePicker();
    static const XamlTypeDeclaration& declaration();
    static std::string registerType(Framework&);
    static const PropertyDescriptor& selectedTimeProperty();
    std::optional<std::chrono::minutes> selectedTime() const;
    PropertySetResult setSelectedTime(std::optional<std::chrono::minutes>);
    i32 minuteIncrement() const;
    PropertySetResult setMinuteIncrement(i32);
    std::string clockIdentifier() const;
    PropertySetResult setClockIdentifier(std::string);
    u64 addSelectedTimeChanged(
        std::function<void(std::optional<std::chrono::minutes>, std::optional<std::chrono::minutes>)>);
};
} // namespace velora
