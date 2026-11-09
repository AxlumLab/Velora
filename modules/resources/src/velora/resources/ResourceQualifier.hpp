#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <velora/globals.hpp>
#include <velora/resources/ResourceContext.hpp>

// Resource qualifiers (R08.2): which candidate of one resource path a given context resolves to.
//
// A package may carry the same logical resource several times — "Assets/Logo.png" at scale 100,
// 150 and 200, "Strings/App" in en-US and zh-CN, a brush in the light and in the dark theme. Each
// copy is written with the qualifiers of the variant it is, and a lookup ranks every candidate
// against the loading ResourceContext and takes the best.
//
// Ranking is a fixed order of axes, not a sum, because UWP says which qualifier dominates when two
// candidates differ on more than one:
//
//   language  exact tag > parent tag ("zh" for "zh-CN") > neutral ("und")
//   theme     the context's own theme > theme-neutral
//   scale     exact > closest larger > closest smaller; no qualifier counts as 100
//
// Contrast is a requirement rather than a preference: contrast-high candidates serve only a
// high-contrast context and the others only a normal one. A candidate that cannot serve the context
// at all (another language, the other theme, the wrong contrast) is rejected, not outranked.

namespace velora
{

enum class ResourceContrast : u8
{
    kNone = 0, // no contrast qualifier: usable in both contexts
    kHigh = 1, // "contrast-high": usable only in a high-contrast context
};

enum class ResourceThemeQualifier : u8
{
    kNone = 0, // no theme qualifier: usable in every theme
    kLight = 1,
    kDark = 2,
};

// Fixed point so a scale qualifier compares exactly and needs no floats: 100, 150, 200...
// Zero means "no scale qualifier" (usable at every scale).
using ResourceScale = u32;

struct ResourceQualifier
{
    std::string language = "und"; // BCP-47; "und" is the language-neutral candidate
    ResourceContrast contrast = ResourceContrast::kNone;
    ResourceThemeQualifier theme = ResourceThemeQualifier::kNone;
    ResourceScale scale = 0;

    friend bool operator==(const ResourceQualifier&, const ResourceQualifier&) = default;
};

// The language subtag rule: candidate "zh-Hans-CN" serves a requested "zh-Hans-CN" exactly, a
// requested "zh-Hans" or "zh" as a parent tag, and "und" serves anything as the neutral fallback.
// Returns 2 for an exact match, 1 for a parent, 0 for "und", and nullopt when the candidate's tag
// cannot serve the request at all (candidate "de-DE", requested "zh-CN").
[[nodiscard]] std::optional<u32> languageMatchScore(std::string_view candidate, std::string_view requested);

// The total rank of `candidate` for `context`; higher wins. nullopt when the candidate is unusable
// (see above), which the caller drops before ranking.
[[nodiscard]] std::optional<u64> qualifierScore(const ResourceQualifier& candidate,
                                                const ResourceContext& context);
// The 100-based scale the context asks for: "scale-150" for a 1.5 rasterization scale.
[[nodiscard]] ResourceScale scaleOf(f32 rasterizationScale) noexcept;

// The canonical spelling a candidate is written with in an index: "lang-en-US_scale-150_theme-dark".
// "und", no contrast and no scale are omitted, so the neutral qualifier spells as "". Round-trips
// through parseQualifiers.
[[nodiscard]] std::string qualifierString(const ResourceQualifier& qualifier);

// Parses the spelling above. "language-" is accepted as an alias of "lang-" and vice versa.
// Unknown segments are rejected rather than ignored, so a typo in a resource name cannot silently
// produce a candidate that only ever loses. An empty string is the neutral qualifier.
[[nodiscard]] std::optional<ResourceQualifier> parseQualifiers(std::string_view text);

// The contrast a theme implies: a high-contrast context, or a normal one.
[[nodiscard]] ResourceContrast contrastOf(ResourceTheme theme) noexcept;

} // namespace velora
