#include <string>
#include <vector>
#include <velora/resources/ResourceUri.hpp>

namespace velora
{

namespace
{
constexpr std::string_view kApplicationScheme = "ms-appx://";
constexpr std::string_view kFrameworkScheme = "ms-appx-framework://";
constexpr std::string_view kLooseScheme = "ms-loose://";

// The path after a scheme may carry a leading '/' for the ":///path" spelling.
std::string_view stripLeadingSlashes(std::string_view path) noexcept
{
    while (!path.empty() && path.front() == '/')
        path.remove_prefix(1);
    return path;
}

bool isAbsolutePath(std::string_view path) noexcept
{
    if (path.empty())
        return false;
    if (path.front() == '/' || path.front() == '\\')
        return true;
    // "C:" or a UNC root.
    if (path.size() >= 2 && path[1] == ':')
        return true;
    return false;
}
} // namespace

std::optional<std::string> normalizeResourcePath(std::string_view path)
{
    if (path.empty() || isAbsolutePath(path))
        return std::nullopt;

    std::vector<std::string_view> segments;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::string_view segment =
            path.substr(start, (end == std::string_view::npos ? path.size() : end) - start);
        if (segment.empty() || segment == ".")
        {
            // Empty (a repeated or trailing separator) and "." segments collapse away.
        }
        else if (segment == "..")
        {
            // ".." may cancel a previous segment but must never escape the root.
            if (segments.empty())
                return std::nullopt;
            segments.pop_back();
        }
        else
        {
            segments.push_back(segment);
        }
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    if (segments.empty())
        return std::nullopt;

    std::string normalized;
    for (const auto segment : segments)
    {
        if (!normalized.empty())
            normalized.push_back('/');
        normalized.append(segment);
    }
    return normalized;
}

std::optional<ResourceUri> ResourceUri::parse(std::string_view text)
{
    struct Scheme
    {
        std::string_view prefix;
        ResourceNamespace nameSpace;
    };
    // Longest prefix first: "ms-appx-framework://" also starts with "ms-appx" plus a dash only,
    // so "ms-appx://" cannot match it — but the order keeps that explicit.
    static constexpr Scheme kSchemes[] = {
        {kFrameworkScheme, ResourceNamespace::kFramework},
        {kApplicationScheme, ResourceNamespace::kApplication},
        {kLooseScheme, ResourceNamespace::kLoose},
    };

    for (const auto& scheme : kSchemes)
    {
        if (!text.starts_with(scheme.prefix))
            continue;
        return fromPath(scheme.nameSpace, stripLeadingSlashes(text.substr(scheme.prefix.size())));
    }

    // No recognized scheme: reject rather than guess at a path spelling.
    if (text.find("://") != std::string_view::npos)
        return std::nullopt;
    return fromPath(ResourceNamespace::kApplication, stripLeadingSlashes(text));
}

std::optional<ResourceUri> ResourceUri::fromPath(ResourceNamespace nameSpace, std::string_view relativePath)
{
    const auto normalized = normalizeResourcePath(relativePath);
    if (!normalized)
        return std::nullopt;
    ResourceUri uri;
    uri.m_nameSpace = nameSpace;
    uri.m_path = *normalized;
    return uri;
}

std::string ResourceUri::toString() const
{
    if (m_path.empty())
        return {};
    std::string scheme;
    switch (m_nameSpace)
    {
    case ResourceNamespace::kApplication:
        scheme = kApplicationScheme;
        break;
    case ResourceNamespace::kFramework:
        scheme = kFrameworkScheme;
        break;
    case ResourceNamespace::kLoose:
        scheme = kLooseScheme;
        break;
    }
    return scheme + "/" + m_path;
}

} // namespace velora
