// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Derived from WinUI 2.3 InitialsGenerator.cpp; WinRT Contact access is intentionally absent.
#include <algorithm>
#include <array>
#include <cwctype>
#include <vector>
#include <velora/fluent/InitialsGenerator.hpp>
#include <velora/text/Encoding.hpp>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace velora
{
namespace
{
enum class CharacterType
{
    kOther,
    kStandard,
    kSymbolic,
    kGlyph
};
struct Range
{
    char32_t first, last;
    CharacterType type;
};
constexpr Range kRanges[] = {
    {0x0250, 0x02AF, CharacterType::kGlyph},      {0x0600, 0x06FF, CharacterType::kGlyph},
    {0x0750, 0x077F, CharacterType::kGlyph},      {0x08A0, 0x08FF, CharacterType::kGlyph},
    {0xFB50, 0xFDFF, CharacterType::kGlyph},      {0xFE70, 0xFEFF, CharacterType::kGlyph},
    {0x0900, 0x097F, CharacterType::kGlyph},      {0xA8E0, 0xA8FF, CharacterType::kGlyph},
    {0x0980, 0x09FF, CharacterType::kGlyph},      {0x0A00, 0x0A7F, CharacterType::kGlyph},
    {0x0A80, 0x0AFF, CharacterType::kGlyph},      {0x0B00, 0x0B7F, CharacterType::kGlyph},
    {0x0B80, 0x0BFF, CharacterType::kGlyph},      {0x0C00, 0x0C7F, CharacterType::kGlyph},
    {0x0C80, 0x0CFF, CharacterType::kGlyph},      {0x0D00, 0x0D7F, CharacterType::kGlyph},
    {0x0D80, 0x0DFF, CharacterType::kGlyph},      {0x0E00, 0x0E7F, CharacterType::kGlyph},
    {0x0E80, 0x0EFF, CharacterType::kGlyph},      {0x4E00, 0x9FFF, CharacterType::kSymbolic},
    {0x3400, 0x4DBF, CharacterType::kSymbolic},   {0x20000, 0x2A6DF, CharacterType::kSymbolic},
    {0x2A700, 0x2B73F, CharacterType::kSymbolic}, {0x2B740, 0x2B81F, CharacterType::kSymbolic},
    {0x2E80, 0x2EFF, CharacterType::kSymbolic},   {0x3000, 0x303F, CharacterType::kSymbolic},
    {0x31C0, 0x31EF, CharacterType::kSymbolic},   {0x3200, 0x32FF, CharacterType::kSymbolic},
    {0x3300, 0x33FF, CharacterType::kSymbolic},   {0xF900, 0xFAFF, CharacterType::kSymbolic},
    {0xFE30, 0xFE4F, CharacterType::kSymbolic},   {0x2F800, 0x2FA1F, CharacterType::kSymbolic},
    {0x0370, 0x03FF, CharacterType::kSymbolic},   {0x0590, 0x05FF, CharacterType::kSymbolic},
    {0x0530, 0x058F, CharacterType::kSymbolic},   {0x0001, 0x007F, CharacterType::kStandard},
    {0x0080, 0x00FF, CharacterType::kStandard},   {0x0100, 0x017F, CharacterType::kStandard},
    {0x0180, 0x024F, CharacterType::kStandard},   {0x2C60, 0x2C7F, CharacterType::kStandard},
    {0xA720, 0xA7FF, CharacterType::kStandard},   {0xAB30, 0xAB6F, CharacterType::kStandard},
    {0x1E00, 0x1EFF, CharacterType::kStandard},   {0x0400, 0x04FF, CharacterType::kStandard},
    {0x0500, 0x052F, CharacterType::kStandard},   {0x0300, 0x036F, CharacterType::kStandard}};

char32_t scalar(std::u16string_view text, usize& index)
{
    char32_t value = text[index++];
    if (value >= 0xD800 && value <= 0xDBFF && index < text.size())
        value = 0x10000 + ((value - 0xD800) << 10) + (text[index++] - 0xDC00);
    return value;
}

CharacterType classify(std::u16string_view text)
{
    CharacterType result = CharacterType::kOther;
    usize index = 0;
    for (int count = 0; count < 3 && index < text.size(); ++count)
    {
        const auto value = scalar(text, index);
        if (!value || value == 0xFEFF)
            break;
        for (const auto& range : kRanges)
            if (value >= range.first && value <= range.last)
            {
                result = std::max(result, range.type);
                break;
            }
    }
    return result;
}

std::u16string firstCharacter(std::u16string_view word)
{
    if (word.empty())
        return {};
    usize start = 0;
    while (start < word.size())
    {
        const auto c = word[start];
        if ((c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) || (c >= 0x7B && c <= 0x7E))
            ++start;
        else
            break;
    }
    if (start == word.size())
        start = 0;
    usize end = start;
    scalar(word, end);
    while (end < word.size() && word[end] >= 0x0300 && word[end] <= 0x036F)
        ++end;
    return std::u16string(word.substr(start, end - start));
}

std::u16string uppercase(std::u16string text)
{
#if defined(_WIN32)
    const std::wstring wide(text.begin(), text.end());
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wide.data(),
                                   static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
    if (size > 0)
    {
        std::wstring result(static_cast<usize>(size), L'\0');
        if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, wide.data(), static_cast<int>(wide.size()),
                          result.data(), size, nullptr, nullptr, 0))
            return std::u16string(result.begin(), result.end());
    }
#else
    for (auto& c : text)
        c = static_cast<char16_t>(std::towupper(c));
#endif
    return text;
}
} // namespace

std::string InitialsGenerator::fromDisplayName(std::string_view name)
{
    auto text = utf8ToUtf16(name);
    if (classify(text) != CharacterType::kStandard)
        return {};
    // WinUI removes one qualifier only when its closing bracket is the final code unit.
    for (const auto pair : {u"{}", u"()", u"[]"})
        if (!text.empty() && text.back() == pair[1])
            if (const auto at = text.find_last_of(pair[0]); at != std::u16string::npos)
            {
                text.erase(at);
                break;
            }
    std::vector<std::u16string_view> words;
    usize start = 0;
    for (int count = 0; count < 25 && start < text.size(); ++count)
    {
        const auto end = std::min(text.find(u' ', start), text.size());
        if (end > start)
            words.push_back(std::u16string_view(text).substr(start, end - start));
        start = end + 1;
    }
    if (words.empty())
        return {};
    auto result = firstCharacter(words.front());
    if (words.size() > 1)
        result += firstCharacter(words.back());
    return utf16ToUtf8(uppercase(std::move(result)));
}
} // namespace velora
