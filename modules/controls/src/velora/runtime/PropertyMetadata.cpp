#include "velora/runtime/PropertyMetadata.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <velora/globals.hpp>

namespace velora
{
namespace
{

bool oneOf(std::string_view value, std::initializer_list<std::string_view> options)
{
    return std::ranges::find(options, value) != options.end();
}

std::string_view stripSuffix(std::string_view text, std::string_view letters)
{
    while (!text.empty() && letters.find(text.back()) != std::string_view::npos)
        text.remove_suffix(1);
    return text;
}

} // namespace

PropertyValue parseDefault(std::string_view type, std::string_view text)
{
    while (type.starts_with("const "))
        type.remove_prefix(6);
    if (type.ends_with("&"))
        type.remove_suffix(1);
    if (type.starts_with("velora::"))
        type.remove_prefix(8);
    const bool boolean = type == "bool";
    const bool floating = oneOf(type, {"float", "double", "f32", "f64"});
    const bool integral =
        oneOf(type, {"int",          "long",          "short",         "unsigned",    "std::int32_t",
                     "std::int64_t", "std::uint32_t", "std::uint64_t", "std::size_t", "int32_t",
                     "int64_t",      "uint32_t",      "size_t",        "i8",          "u8",
                     "i16",          "u16",           "i32",           "u32",         "i64",
                     "u64",          "usize",         "isize",         "uptr",        "iptr"});
    const bool textual = oneOf(type, {"std::string", "std::string_view", "const char*", "char const*"});
    if (text.empty())
    {
        if (boolean)
            return PropertyValue(false);
        if (floating)
            return PropertyValue(0.0);
        if (integral)
            return PropertyValue(i64{0});
        if (textual)
            return PropertyValue(std::string());
        return {};
    }
    if (boolean)
    {
        if (text == "true" || text == "false")
            return PropertyValue(text == "true");
    }
    else if (floating)
    {
        double value = 0;
        const auto number = stripSuffix(text, "fF");
        if (std::from_chars(number.data(), number.data() + number.size(), value).ptr ==
            number.data() + number.size())
            return PropertyValue(value);
    }
    else if (integral)
    {
        i64 value = 0;
        const auto number = stripSuffix(text, "uUlL");
        if (std::from_chars(number.data(), number.data() + number.size(), value).ptr ==
            number.data() + number.size())
            return PropertyValue(value);
    }
    else if (textual && text.size() >= 2 && text.front() == '"' && text.back() == '"')
    {
        return PropertyValue(std::string(text.substr(1, text.size() - 2)));
    }
    throw std::invalid_argument("default '" + std::string(text) + "' is not a valid " + std::string(type));
}

RegisteredType registerType(PropertyRegistry& properties, EventRegistry& events, const TypeMetadata& type)
{
    RegisteredType out;
    for (const auto& p : type.properties)
    {
        PropertyFlags flags = PropertyFlags::kNone;
        if (p.hasFlag("Measure"))
            flags = flags | PropertyFlags::kMeasure;
        if (p.hasFlag("Arrange"))
            flags = flags | PropertyFlags::kArrange;
        if (p.hasFlag("Paint"))
            flags = flags | PropertyFlags::kPaint;
        const PropertyKind kind = p.hasFlag("Attached")  ? PropertyKind::kAttached
                                  : p.hasFlag("Content") ? PropertyKind::kContent
                                                         : PropertyKind::kNormal;
        const auto& descriptor =
            properties.define(type.qualifiedName, p.name, parseDefault(p.type, p.defaultValue), flags,
                              p.hasFlag("Inherited"), kind);
        out.properties.push_back(&descriptor);
        if (kind == PropertyKind::kContent)
        {
            if (out.content)
                throw std::invalid_argument(type.qualifiedName + " declares more than one Content property");
            out.content = &descriptor;
        }
    }
    for (const auto& e : type.events)
        out.events.push_back(&events.define(type.qualifiedName, e.name, e.signature));
    return out;
}

} // namespace velora
