// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
namespace velora
{
std::optional<std::chrono::year_month_day> parseCalendarDate(std::string_view);
std::string formatCalendarDate(std::chrono::year_month_day);
std::chrono::year_month_day calendarToday();
} // namespace velora
