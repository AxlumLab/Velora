#include <stdexcept>
#include <velora/text/Encoding.hpp>

namespace velora
{
namespace
{
void appendUtf8(std::string& output, u32 codePoint)
{
    if (codePoint <= 0x7F)
    {
        output.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7FF)
    {
        output.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else if (codePoint <= 0xFFFF)
    {
        output.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
    else
    {
        output.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
    }
}
} // namespace

std::u16string utf8ToUtf16(std::string_view utf8)
{
    std::u16string result;
    result.reserve(utf8.size());
    for (usize i = 0; i < utf8.size();)
    {
        const u8 first = static_cast<u8>(utf8[i++]);
        u32 codePoint = first;
        u32 minimum = 0;
        usize remaining = 0;
        if (first <= 0x7F)
        {
            result.push_back(static_cast<char16_t>(first));
            continue;
        }
        if (first >= 0xC2 && first <= 0xDF)
        {
            codePoint = first & 0x1F;
            minimum = 0x80;
            remaining = 1;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            codePoint = first & 0x0F;
            minimum = 0x800;
            remaining = 2;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            codePoint = first & 0x07;
            minimum = 0x10000;
            remaining = 3;
        }
        else
        {
            throw std::runtime_error("Invalid UTF-8 string");
        }
        if (remaining > utf8.size() - i)
            throw std::runtime_error("Invalid UTF-8 string");
        for (usize j = 0; j < remaining; ++j)
        {
            const u8 next = static_cast<u8>(utf8[i++]);
            if ((next & 0xC0) != 0x80)
                throw std::runtime_error("Invalid UTF-8 string");
            codePoint = (codePoint << 6) | (next & 0x3F);
        }
        if (codePoint < minimum || codePoint > 0x10FFFF ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF))
            throw std::runtime_error("Invalid UTF-8 string");
        if (codePoint <= 0xFFFF)
        {
            result.push_back(static_cast<char16_t>(codePoint));
        }
        else
        {
            codePoint -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 | (codePoint >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 | (codePoint & 0x3FF)));
        }
    }
    return result;
}

std::string utf16ToUtf8(std::u16string_view utf16)
{
    std::string result;
    result.reserve(utf16.size());
    for (usize i = 0; i < utf16.size(); ++i)
    {
        u32 codePoint = utf16[i];
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
        {
            if (++i == utf16.size())
                throw std::runtime_error("Invalid UTF-16 string");
            const u32 low = utf16[i];
            if (low < 0xDC00 || low > 0xDFFF)
                throw std::runtime_error("Invalid UTF-16 string");
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
        }
        else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
        {
            throw std::runtime_error("Invalid UTF-16 string");
        }
        appendUtf8(result, codePoint);
    }
    return result;
}

std::u16string utf8ToUtf16Safe(std::string_view utf8) noexcept
{
    try
    {
        return utf8ToUtf16(utf8);
    }
    catch (...)
    {
        return {};
    }
}

std::string utf16ToUtf8Safe(std::u16string_view utf16) noexcept
{
    try
    {
        return utf16ToUtf8(utf16);
    }
    catch (...)
    {
        return {};
    }
}
}
