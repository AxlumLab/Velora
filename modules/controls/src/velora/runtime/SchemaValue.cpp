#include "velora/runtime/SchemaValue.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <vector>
#include <velora/globals.hpp>

namespace velora
{
namespace
{

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                              [](char x, char y)
                                              {
                                                  return std::tolower(static_cast<unsigned char>(x)) ==
                                                         std::tolower(static_cast<unsigned char>(y));
                                              });
}

std::vector<std::string_view> splitList(std::string_view text)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == ',' || text[i] == ' ')
        {
            if (i > start)
                out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

} // namespace

std::optional<double> parseNumber(std::string_view text)
{
    text = trim(text);
    double value = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || r.ec != std::errc{} || r.ptr != text.data() + text.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::optional<i64> parseColor(std::string_view text)
{
    text = trim(text);
    static constexpr std::pair<std::string_view, u32> kNamed[] = {
        {"Transparent", 0x00FFFFFFu}, {"Black", 0xFF000000u},     {"White", 0xFFFFFFFFu},
        {"Red", 0xFFFF0000u},         {"Green", 0xFF008000u},     {"Blue", 0xFF0000FFu},
        {"Gray", 0xFF808080u},        {"LightGray", 0xFFD3D3D3u}, {"DarkGray", 0xFFA9A9A9u},
        {"Yellow", 0xFFFFFF00u},      {"Orange", 0xFFFFA500u}};
    for (const auto& [name, value] : kNamed)
        if (equalsIgnoreCase(text, name))
            return value;
    if (text.size() < 2 || text[0] != '#')
        return std::nullopt;
    const auto digits = text.substr(1);
    u32 value = 0;
    const auto r = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (r.ec != std::errc{} || r.ptr != digits.data() + digits.size())
        return std::nullopt;
    const auto expand = [](u32 nibble) { return nibble << 4 | nibble; };
    switch (digits.size())
    {
    case 3:
        return 0xFF000000u | expand(value >> 8 & 0xF) << 16 | expand(value >> 4 & 0xF) << 8 |
               expand(value & 0xF);
    case 4:
        return expand(value >> 12 & 0xF) << 24 | expand(value >> 8 & 0xF) << 16 |
               expand(value >> 4 & 0xF) << 8 | expand(value & 0xF);
    case 6:
        return 0xFF000000u | value;
    case 8:
        return value;
    default:
        return std::nullopt;
    }
}

std::optional<Thickness> parseThickness(std::string_view text)
{
    std::vector<double> v;
    for (auto part : splitList(text))
    {
        const auto n = parseNumber(part);
        if (!n)
            return std::nullopt;
        v.push_back(*n);
    }
    switch (v.size())
    {
    case 1:
        return Thickness{v[0], v[0], v[0], v[0]};
    case 2:
        return Thickness{v[0], v[1], v[0], v[1]};
    case 4:
        return Thickness{v[0], v[1], v[2], v[3]};
    default:
        return std::nullopt;
    }
}

std::optional<GridLength> parseGridLength(std::string_view text)
{
    text = trim(text);
    if (equalsIgnoreCase(text, "Auto"))
        return GridLength{GridLength::Unit::kAuto, 0};
    if (text.ends_with('*'))
    {
        if (text.size() == 1)
            return GridLength{GridLength::Unit::kStar, 1};
        const auto weight = parseNumber(text.substr(0, text.size() - 1));
        if (!weight || *weight <= 0)
            return std::nullopt;
        return GridLength{GridLength::Unit::kStar, *weight};
    }
    const auto pixels = parseNumber(text);
    if (!pixels || *pixels < 0)
        return std::nullopt;
    return GridLength{GridLength::Unit::kPixel, *pixels};
}

std::string_view kindName(ValueKind kind)
{
    switch (kind)
    {
    case ValueKind::kString:
        return "String";
    case ValueKind::kDouble:
        return "Double";
    case ValueKind::kInteger:
        return "Int32";
    case ValueKind::kBoolean:
        return "Boolean";
    case ValueKind::kThickness:
        return "Thickness";
    case ValueKind::kBrush:
        return "Brush";
    case ValueKind::kGridLengths:
        return "GridLength list";
    case ValueKind::kEnum:
        return "enumeration";
    case ValueKind::kVisibility:
        return "Visibility";
    case ValueKind::kObject:
        return "object";
    case ValueKind::kCornerRadius:
        return "CornerRadius";
    }
    return "?";
}

std::variant<PropertyValue, std::string> parseValue(const SchemaProperty& property, std::string_view text)
{
    const std::string name(property.name);
    const std::string spelled(text);
    switch (property.kind)
    {
    case ValueKind::kString:
        return PropertyValue(std::string(text));
    case ValueKind::kDouble:
    {
        if (trim(text).empty() || equalsIgnoreCase(trim(text), "Auto"))
            return PropertyValue();
        if (const auto n = parseNumber(text))
            return PropertyValue(*n);
        return "'" + spelled + "' is not a number (" + name + " is a Double)";
    }
    case ValueKind::kInteger:
    {
        i64 value = 0;
        const auto t = trim(text);
        const auto r = std::from_chars(t.data(), t.data() + t.size(), value);
        if (!t.empty() && r.ec == std::errc{} && r.ptr == t.data() + t.size())
            return PropertyValue(value);
        return "'" + spelled + "' is not an integer (" + name + " is an Int32)";
    }
    case ValueKind::kBoolean:
        if (equalsIgnoreCase(trim(text), "true"))
            return PropertyValue(true);
        if (equalsIgnoreCase(trim(text), "false"))
            return PropertyValue(false);
        return "'" + spelled + "' is not True or False (" + name + " is a Boolean)";
    case ValueKind::kThickness:
        if (const auto t = parseThickness(text))
            return PropertyValue(*t);
        return "'" + spelled + "' is not a Thickness (use \"4\", \"8,4\" or \"1,2,3,4\")";
    case ValueKind::kBrush:
        if (trim(text).empty())
            return PropertyValue();
        if (const auto c = parseColor(text))
            return PropertyValue(*c);
        return "'" + spelled + "' is not a color (use #AARRGGBB, #RRGGBB or a named color)";
    case ValueKind::kGridLengths:
    {
        GridLengths out;
        for (auto part : splitList(text))
        {
            const auto g = parseGridLength(part);
            if (!g)
                return "'" + std::string(part) + "' is not a GridLength (use Auto, *, 2* or pixels)";
            out.push_back(*g);
        }
        return PropertyValue(std::move(out));
    }
    case ValueKind::kEnum:
    case ValueKind::kVisibility:
    {
        const auto t = trim(text);
        if (t.empty() && property.kind == ValueKind::kEnum)
            return PropertyValue();
        std::string_view choices = property.choices;
        for (std::size_t start = 0; start <= choices.size();)
        {
            const auto end = std::min(choices.find('|', start), choices.size());
            if (choices.substr(start, end - start) == t)
                return property.kind == ValueKind::kVisibility ? PropertyValue(t == "Visible")
                                                               : PropertyValue(std::string(t));
            start = end + 1;
        }
        std::string list(choices);
        std::ranges::replace(list, '|', ',');
        return "'" + spelled + "' is not a valid " + name + " (expected one of " + list + ")";
    }
    case ValueKind::kCornerRadius:
    {
        const auto t = parseThickness(text);
        if (!t)
            return "'" + spelled + "' is not a CornerRadius (use \"4\" or \"4,4,4,4\")";
        if (t->left != t->top || t->left != t->right || t->left != t->bottom)
            return "'" + spelled + "': only uniform corner radii are supported";
        return PropertyValue(t->left);
    }
    case ValueKind::kObject:
        if (trim(text).empty())
            return PropertyValue();
        return name + " takes an object; set it with {x:Bind} or {Binding}";
    }
    return std::string("unsupported property kind");
}

} // namespace velora
