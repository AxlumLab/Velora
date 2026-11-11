// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#include <array>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <velora/fluent/CalendarDate.hpp>
#include <velora/globals.hpp>
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
} // namespace
std::optional<std::chrono::year_month_day> parseCalendarDate(std::string_view text)
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
std::string formatCalendarDate(std::chrono::year_month_day value)
{
    if (!value.ok() || static_cast<i32>(value.year()) < 1601 || static_cast<i32>(value.year()) > 9999)
        throw std::invalid_argument("DatePicker requires a valid Gregorian date in 1601..9999");
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "%04d-%02u-%02u", static_cast<i32>(value.year()),
                  static_cast<unsigned>(value.month()), static_cast<unsigned>(value.day()));
    return text.data();
}
std::chrono::year_month_day calendarToday()
{
    const auto now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return std::chrono::year{local.tm_year + 1900} / (local.tm_mon + 1) / local.tm_mday;
}
} // namespace velora
