#pragma once

#include <string>
#include <velora/globals.hpp>

// The qualifiers resource lookup depends on, per XAML root (R04.5, R08.2).
//
// Each XamlRoot has its own context: two windows on monitors with different DPI, or with
// different requested themes, resolve the same ResourceUri to different candidates. A context is
// a small value; what is shared between roots is the immutable index it is evaluated against.

namespace velora
{

enum class ResourceTheme : u8
{
    kLight,
    kDark,
    kHighContrast
};

struct ResourceContext
{
    // Device pixels per DIP: 1.0, 1.5, 2.0... (UWP RasterizationScale; "scale-150").
    f32 scale = 1.0f;
    ResourceTheme theme = ResourceTheme::kLight;
    // BCP-47 language tag, e.g. "en-US", "zh-CN".
    std::string language = "en-US";

    friend bool operator==(const ResourceContext&, const ResourceContext&) = default;
};

} // namespace velora
