#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <velora/globals.hpp>
#include <velora/runtime/ElementTree.hpp>
#include <velora/runtime/FrameworkSchema.hpp>

// Conversion of XAML attribute text to property values, shared by the runtime (schema
// defaults) and the XAML compiler (literal attribute values).
namespace velora
{

std::optional<i64> parseColor(std::string_view text); // 0xAARRGGBB
std::optional<Thickness> parseThickness(std::string_view text);
std::optional<GridLength> parseGridLength(std::string_view text);
std::optional<double> parseNumber(std::string_view text);

// The value, or an error message naming the expected form. "Auto" and an empty Double are
// returned as an empty (unset) value.
std::variant<PropertyValue, std::string> parseValue(const SchemaProperty& property, std::string_view text);

std::string_view kindName(ValueKind kind);

} // namespace velora
