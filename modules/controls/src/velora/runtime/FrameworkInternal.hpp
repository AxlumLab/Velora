// Private framework helpers; no public header dependency.
#pragma once

#include "velora/runtime/Framework.hpp"
#include "velora/runtime/SchemaValue.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <velora/composition/Compositor.hpp>
#include <velora/globals.hpp>
#include <velora/graphics/Canvas.hpp>

namespace velora
{
namespace
{

constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr i64 kAccent = 0xFF0078D4;

Color argb(i64 value, float alpha = 1.0f)
{
    const auto channel = [&](int shift) { return static_cast<float>((value >> shift) & 0xFF) / 255.0f; };
    return {channel(16), channel(8), channel(0), channel(24) * alpha};
}

PropertyFlags flagsOf(unsigned bits)
{
    return static_cast<PropertyFlags>(bits & (kSchemaMeasure | kSchemaArrange | kSchemaPaint));
}

// UTF-8 code point boundaries.
std::size_t previousBoundary(std::string_view s, std::size_t at)
{
    if (at == 0)
        return 0;
    --at;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80)
        --at;
    return at;
}
std::size_t nextBoundary(std::string_view s, std::size_t at)
{
    if (at >= s.size())
        return s.size();
    ++at;
    while (at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80)
        ++at;
    return at;
}
std::size_t clampBoundary(std::string_view s, std::size_t at)
{
    at = std::min(at, s.size());
    while (at > 0 && at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80)
        --at;
    return at;
}
// Byte length of the first `count` code points.
std::size_t codePointPrefix(std::string_view s, std::size_t count)
{
    std::size_t at = 0;
    while (count-- > 0 && at < s.size())
        at = nextBoundary(s, at);
    return at;
}

} // namespace

} // namespace velora
