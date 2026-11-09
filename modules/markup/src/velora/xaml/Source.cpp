#include "Source.hpp"

#include <algorithm>
#include <cctype>
#include <velora/globals.hpp>

namespace velora
{

std::optional<std::size_t> invalidUtf8(std::string_view text)
{
    const auto* s = reinterpret_cast<const unsigned char*>(text.data());
    for (std::size_t i = 0; i < text.size();)
    {
        const unsigned char c = s[i];
        std::size_t n = 0;
        char32_t cp = 0;
        if (c < 0x80)
        {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0)
        {
            n = 1;
            cp = c & 0x1F;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            n = 2;
            cp = c & 0x0F;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            n = 3;
            cp = c & 0x07;
        }
        else
            return i;
        for (std::size_t k = 1; k <= n; ++k)
        {
            if (i + k >= text.size() || (s[i + k] & 0xC0) != 0x80)
                return i;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        const char32_t minimum = n == 1 ? 0x80 : n == 2 ? 0x800 : 0x10000;
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return i;
        i += n + 1;
    }
    return std::nullopt;
}

std::vector<u32> codePointOffsets(std::string_view text)
{
    std::vector<u32> out;
    out.reserve(text.size() + 1);
    for (std::size_t i = 0; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
            out.push_back(static_cast<u32>(i));
    out.push_back(static_cast<u32>(text.size()));
    return out;
}

SourceMap::SourceMap(std::string_view text) : m_text(text), m_codePoints(codePointOffsets(text))
{
    m_lineStarts.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n')
            m_lineStarts.push_back(i + 1);
}

Position SourceMap::at(std::size_t offset) const
{
    offset = std::min(offset, m_text.size());
    const auto line =
        std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), offset) - m_lineStarts.begin();
    const std::size_t start = m_lineStarts[static_cast<std::size_t>(line - 1)];
    u32 column = 1;
    for (std::size_t i = start; i < offset; ++i)
        if ((static_cast<unsigned char>(m_text[i]) & 0xC0) != 0x80)
            ++column;
    return {static_cast<u32>(line), column, static_cast<u32>(offset)};
}

std::size_t SourceMap::byteOfCodePoint(std::size_t index) const
{
    return m_codePoints[std::min(index, m_codePoints.size() - 1)];
}

MappedText MappedText::of(std::string_view source, std::size_t offset)
{
    MappedText out{std::string(source), {}};
    out.origin.resize(source.size() + 1);
    for (std::size_t i = 0; i <= source.size(); ++i)
        out.origin[i] = static_cast<u32>(offset + i);
    return out;
}

MappedText MappedText::slice(std::size_t begin, std::size_t end) const
{
    end = std::min(end, text.size());
    begin = std::min(begin, end);
    MappedText out{text.substr(begin, end - begin), {}};
    out.origin.assign(origin.begin() + static_cast<std::ptrdiff_t>(begin),
                      origin.begin() + static_cast<std::ptrdiff_t>(end) + 1);
    return out;
}

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80)
    {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::variant<MappedText, DecodeError> decode(const MappedText& raw, DecodeMode mode)
{
    MappedText out;
    const std::string_view s = raw.text;
    const auto emit = [&](std::string_view bytes, std::size_t from)
    {
        for (char c : bytes)
        {
            out.text.push_back(c);
            out.origin.push_back(raw.origin[from]);
        }
    };
    for (std::size_t i = 0; i < s.size();)
    {
        const char c = s[i];
        if (c == '\r')
        {
            // CRLF and lone CR are one line break.
            emit(mode == DecodeMode::kAttribute ? " " : "\n", i);
            i += (i + 1 < s.size() && s[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        if (mode == DecodeMode::kAttribute && (c == '\n' || c == '\t'))
        {
            emit(" ", i);
            ++i;
            continue;
        }
        if (c != '&')
        {
            emit(s.substr(i, 1), i);
            ++i;
            continue;
        }
        // A reference is '&' name ';' or '&#' digits ';'; anything else is a bare ampersand.
        std::size_t end = i + 1;
        if (end < s.size() && s[end] == '#')
            ++end;
        while (end < s.size() && (std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_' ||
                                  s[end] == '-' || s[end] == '.'))
            ++end;
        if (end == i + 1 || end >= s.size() || s[end] != ';')
            return DecodeError{raw.origin[i], "unescaped '&' (write &amp;)"};
        const auto body = s.substr(i + 1, end - i - 1);
        static constexpr std::pair<std::string_view, std::string_view> kNamed[] = {
            {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}};
        bool done = false;
        for (const auto& [name, text] : kNamed)
        {
            if (body == name)
            {
                emit(text, i);
                done = true;
            }
        }
        if (!done)
        {
            if (body.size() < 2 || body[0] != '#')
                return DecodeError{raw.origin[i], "undefined entity '&" + std::string(body) +
                                                      ";' (DTD entities are not supported)"};
            const bool hex = body[1] == 'x' || body[1] == 'X';
            const auto digits = body.substr(hex ? 2 : 1);
            char32_t cp = 0;
            bool valid = !digits.empty();
            for (char d : digits)
            {
                int v = d >= '0' && d <= '9'          ? d - '0'
                        : hex && d >= 'a' && d <= 'f' ? d - 'a' + 10
                        : hex && d >= 'A' && d <= 'F' ? d - 'A' + 10
                                                      : -1;
                if (v < 0 || cp > 0x10FFFF)
                {
                    valid = false;
                    break;
                }
                cp = cp * (hex ? 16 : 10) + static_cast<char32_t>(v);
            }
            const bool xmlChar = cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) ||
                                 (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
            if (!valid || !xmlChar)
                return DecodeError{raw.origin[i],
                                   "invalid character reference '&" + std::string(body) + ";'"};
            std::string bytes;
            appendUtf8(bytes, cp);
            emit(bytes, i);
        }
        i = end + 1;
    }
    out.origin.push_back(raw.origin[s.size()]);
    return out;
}

} // namespace velora
