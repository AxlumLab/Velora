#pragma once

#include <velora/globals.hpp>

// Internal: source positions, UTF-8 handling and XML value decoding for the XAML front end.

#include "velora/xaml/Ast.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace velora
{

// Byte offset of the first invalid UTF-8 sequence, if any.
std::optional<std::size_t> invalidUtf8(std::string_view text);

// Maps byte offsets of one source text to line/column positions.
class SourceMap
{
public:
    explicit SourceMap(std::string_view text);
    Position at(std::size_t offset) const;
    Span span(std::size_t begin, std::size_t end) const
    {
        return {at(begin), at(end)};
    }
    // Byte offset of a code point index (ANTLR token indices count code points).
    std::size_t byteOfCodePoint(std::size_t index) const;

private:
    std::string_view m_text;
    std::vector<std::size_t> m_lineStarts;
    std::vector<u32> m_codePoints; // byte offset of each code point, plus the end
};

// Text derived from the source (decoded values, slices) that remembers, for every byte,
// the source byte offset it came from. origin.size() == text.size() + 1.
struct MappedText
{
    std::string text;
    std::vector<u32> origin;

    static MappedText of(std::string_view source, std::size_t offset);
    MappedText slice(std::size_t begin, std::size_t end) const;
    std::size_t sourceOffset(std::size_t byte) const
    {
        return origin[std::min(byte, text.size())];
    }
};

// Byte offset of each code point of `text`, plus the end.
std::vector<u32> codePointOffsets(std::string_view text);

enum class DecodeMode
{
    kAttribute,
    kText
};

// Decodes the five predefined entities and character references. Attribute mode applies XML
// attribute-value normalization (literal tab/CR/LF become spaces); text mode normalizes
// line endings to LF. On error returns the source offset of the faulty reference.
struct DecodeError
{
    std::size_t offset;
    std::string message;
};
std::variant<MappedText, DecodeError> decode(const MappedText& raw, DecodeMode mode);

void appendUtf8(std::string& out, char32_t codePoint);

} // namespace velora
