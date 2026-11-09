#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>
#include <velora/resources/ResourceQualifier.hpp>

namespace velora
{

namespace
{
std::vector<std::string_view> splitTag(std::string_view tag)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= tag.size())
    {
        const std::size_t end = tag.find('-', start);
        const std::size_t stop = end == std::string_view::npos ? tag.size() : end;
        if (stop > start)
            parts.push_back(tag.substr(start, stop - start));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return parts;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const auto lower = [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
        if (lower(a[i]) != lower(b[i]))
            return false;
    }
    return true;
}

// A score packs the axes so that comparing whole scores compares them in priority order:
//   bits 32-33  language (2 exact, 1 parent tag, 0 neutral)
//   bits 30-31  theme    (1 the context's own theme, 0 theme-neutral)
//   bits  0-29  scale    (see scaleBits)
// Contrast is a requirement, not a preference, so it never reaches the score.
constexpr int kLanguageShift = 32;
constexpr int kThemeShift = 30;

// The scale axis follows UWP: the exact scale, else the closest larger one (downscaling an image
// looks better than magnifying it), else the closest smaller one. A candidate without a scale
// qualifier counts as scale 100 and loses a tie to an explicit scale-100 candidate.
u64 scaleBits(const ResourceQualifier& candidate, const ResourceContext& context)
{
    const ResourceScale requested = scaleOf(context.scale);
    const ResourceScale scale = candidate.scale ? candidate.scale : 100;
    const u32 category = scale == requested ? 3u : scale > requested ? 2u : 1u;
    const u32 distance = std::min<u32>(scale > requested ? scale - requested : requested - scale, 0xFFFFu);
    return (u64{category} << 20) | (u64{0xFFFFu - distance} << 1) | (candidate.scale ? 1u : 0u);
}
} // namespace

std::optional<u32> languageMatchScore(std::string_view candidate, std::string_view requested)
{
    if (candidate.empty() || equalsIgnoreCase(candidate, "und"))
        return 0u;
    if (requested.empty() || equalsIgnoreCase(requested, "und"))
        return std::nullopt; // a specific candidate cannot serve a language-neutral context
    if (equalsIgnoreCase(candidate, requested))
        return 2u;
    // A parent tag: the candidate is a proper prefix of the request at a subtag boundary ("zh-Hans"
    // for "zh-Hans-CN"). A longer candidate is not a parent ("zh-Hans-CN" cannot serve "zh").
    if (candidate.size() >= requested.size())
        return std::nullopt;
    const auto requestedParts = splitTag(requested);
    const auto candidateParts = splitTag(candidate);
    if (candidateParts.size() >= requestedParts.size())
        return std::nullopt;
    for (std::size_t i = 0; i < candidateParts.size(); ++i)
        if (!equalsIgnoreCase(candidateParts[i], requestedParts[i]))
            return std::nullopt;
    return 1u;
}

ResourceScale scaleOf(f32 rasterizationScale) noexcept
{
    if (!(rasterizationScale > 0.0f) || !std::isfinite(rasterizationScale))
        return 100;
    const double scaled = std::lround(static_cast<double>(rasterizationScale) * 100.0);
    return static_cast<ResourceScale>(std::clamp<double>(scaled, 1.0, 10000.0));
}

ResourceContrast contrastOf(ResourceTheme theme) noexcept
{
    return theme == ResourceTheme::kHighContrast ? ResourceContrast::kHigh : ResourceContrast::kNone;
}

std::optional<u64> qualifierScore(const ResourceQualifier& candidate, const ResourceContext& context)
{
    const auto language = languageMatchScore(candidate.language, context.language);
    if (!language)
        return std::nullopt;
    if (candidate.contrast != contrastOf(context.theme))
        return std::nullopt; // contrast-high serves only high contrast, and only it does
    u64 theme = 0;
    if (candidate.theme != ResourceThemeQualifier::kNone)
    {
        // A light or dark candidate serves only that theme; in high contrast neither applies.
        const bool matches =
            (candidate.theme == ResourceThemeQualifier::kLight && context.theme == ResourceTheme::kLight) ||
            (candidate.theme == ResourceThemeQualifier::kDark && context.theme == ResourceTheme::kDark);
        if (!matches)
            return std::nullopt;
        theme = 1;
    }
    return (u64{*language} << kLanguageShift) | (theme << kThemeShift) | scaleBits(candidate, context);
}

std::string qualifierString(const ResourceQualifier& qualifier)
{
    std::string out;
    if (!qualifier.language.empty() && !equalsIgnoreCase(qualifier.language, "und"))
    {
        out += "lang-";
        out += qualifier.language;
    }
    if (qualifier.contrast == ResourceContrast::kHigh)
    {
        if (!out.empty())
            out.push_back('_');
        out += "contrast-high";
    }
    if (qualifier.theme != ResourceThemeQualifier::kNone)
    {
        if (!out.empty())
            out.push_back('_');
        out += qualifier.theme == ResourceThemeQualifier::kDark ? "theme-dark" : "theme-light";
    }
    if (qualifier.scale != 0)
    {
        if (!out.empty())
            out.push_back('_');
        out += std::format("scale-{}", qualifier.scale);
    }
    return out;
}

std::optional<ResourceQualifier> parseQualifiers(std::string_view text)
{
    ResourceQualifier qualifier;
    if (text.empty())
        return qualifier;
    std::size_t start = 0;
    while (start <= text.size())
    {
        const std::size_t end = text.find('_', start);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        const std::string_view segment = text.substr(start, stop - start);
        if (segment.empty())
            return std::nullopt; // "a__b" or a trailing '_' is a spelling error, not an empty segment
        const auto dash = [&]() -> std::string_view
        {
            const std::size_t i = segment.find('-');
            return i == std::string_view::npos ? std::string_view{} : segment.substr(i + 1);
        };
        const std::string_view key = dash().empty() ? segment : segment.substr(0, segment.find('-'));
        const std::string_view value = dash();
        if ((key == "lang" || key == "language") && !value.empty())
        {
            // A tag is a sequence of 2-8 character alphanumeric subtags.
            for (const auto part : splitTag(value))
                if (part.size() < 1 || part.size() > 8 ||
                    !std::ranges::all_of(part, [](char c)
                                         { return std::isalnum(static_cast<unsigned char>(c)) != 0; }))
                    return std::nullopt;
            qualifier.language = std::string(value);
        }
        else if (key == "contrast" && (value == "high" || value == "standard"))
        {
            qualifier.contrast = value == "high" ? ResourceContrast::kHigh : ResourceContrast::kNone;
        }
        else if (key == "theme" && (value == "light" || value == "dark"))
        {
            qualifier.theme =
                value == "dark" ? ResourceThemeQualifier::kDark : ResourceThemeQualifier::kLight;
        }
        else if (key == "scale" && !value.empty())
        {
            u32 scale = 0;
            for (const char c : value)
            {
                if (c < '0' || c > '9')
                    return std::nullopt;
                scale = scale * 10 + static_cast<u32>(c - '0');
                if (scale > 10000)
                    return std::nullopt;
            }
            qualifier.scale = scale;
        }
        else
        {
            return std::nullopt;
        }
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return qualifier;
}

} // namespace velora
