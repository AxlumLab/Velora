#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>
#include <velora/resources/ResourceScan.hpp>

namespace velora
{

namespace
{
bool looksLikeQualifier(std::string_view segment) noexcept
{
    for (const std::string_view prefix : {"scale-", "lang-", "language-", "theme-", "contrast-"})
        if (segment.starts_with(prefix))
            return true;
    return false;
}

// Merges `extra` into `into`; false when both set the same axis (two scales on one file).
bool merge(ResourceQualifier& into, const ResourceQualifier& extra)
{
    const ResourceQualifier neutral;
    if (extra.language != neutral.language)
    {
        if (into.language != neutral.language)
            return false;
        into.language = extra.language;
    }
    if (extra.contrast != neutral.contrast)
    {
        if (into.contrast != neutral.contrast)
            return false;
        into.contrast = extra.contrast;
    }
    if (extra.theme != neutral.theme)
    {
        if (into.theme != neutral.theme)
            return false;
        into.theme = extra.theme;
    }
    if (extra.scale != neutral.scale)
    {
        if (into.scale != neutral.scale)
            return false;
        into.scale = extra.scale;
    }
    return true;
}
} // namespace

std::variant<std::pair<std::string, ResourceQualifier>, ResourceScanError>
logicalResourcePath(std::string_view packagePath)
{
    const auto normalized = normalizeResourcePath(packagePath);
    if (!normalized)
        return ResourceScanError{"'" + std::string(packagePath) + "' is not a resource path"};
    std::vector<std::string_view> segments;
    std::string_view rest = *normalized;
    for (std::size_t slash; (slash = rest.find('/')) != std::string_view::npos; rest.remove_prefix(slash + 1))
        segments.push_back(rest.substr(0, slash));
    const std::string_view file = rest;

    ResourceQualifier qualifier;
    std::string logical;
    const auto fail = [&](std::string_view what)
    { return ResourceScanError{"'" + *normalized + "': " + std::string(what)}; };
    for (const auto directory : segments)
    {
        if (looksLikeQualifier(directory))
        {
            const auto parsed = parseQualifiers(directory);
            if (!parsed)
                return fail("malformed qualifier directory '" + std::string(directory) + "'");
            if (!merge(qualifier, *parsed))
                return fail("a qualifier is given twice");
            continue;
        }
        logical.append(directory);
        logical.push_back('/');
    }
    // "Logo.scale-200.png": the qualifier is the second-to-last dot segment.
    const std::size_t lastDot = file.rfind('.');
    const std::size_t qualifierDot = lastDot == std::string_view::npos || lastDot == 0
                                         ? std::string_view::npos
                                         : file.rfind('.', lastDot - 1);
    if (qualifierDot != std::string_view::npos)
    {
        const std::string_view segment = file.substr(qualifierDot + 1, lastDot - qualifierDot - 1);
        if (looksLikeQualifier(segment))
        {
            const auto parsed = parseQualifiers(segment);
            if (!parsed)
                return fail("malformed qualifier '" + std::string(segment) + "'");
            if (!merge(qualifier, *parsed))
                return fail("a qualifier is given twice");
            logical.append(file.substr(0, qualifierDot));
            logical.append(file.substr(lastDot));
            return std::pair{std::move(logical), std::move(qualifier)};
        }
    }
    logical.append(file);
    return std::pair{std::move(logical), std::move(qualifier)};
}

std::variant<ResourceIndex, ResourceScanError>
indexResourceFiles(const std::filesystem::path& root, std::string name, std::string defaultLanguage)
{
    namespace fs = std::filesystem;
    ResourceIndex index;
    index.name = std::move(name);
    index.defaultLanguage = std::move(defaultLanguage);
    std::error_code error;
    if (!fs::is_directory(root, error))
        return ResourceScanError{"'" + root.generic_string() + "' is not a directory"};
    // Sorted, so the index (and a package built from it) does not depend on directory order.
    std::vector<std::string> files;
    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file(error))
            continue;
        const auto extension = it->path().extension().string();
        if (extension == ".resw" || extension == ".vpri" || extension == ".vpak")
            continue;
        files.push_back(it->path().lexically_relative(root).generic_string());
    }
    if (error)
        return ResourceScanError{"cannot list '" + root.generic_string() + "': " + error.message()};
    std::ranges::sort(files);
    for (const auto& file : files)
    {
        auto mapped = logicalResourcePath(file);
        if (auto* e = std::get_if<ResourceScanError>(&mapped))
            return std::move(*e);
        auto& [logical, qualifier] = std::get<0>(mapped);
        if (const auto* existing = index.variants(logical))
            for (const auto& candidate : *existing)
                if (candidate.qualifier == qualifier)
                    return ResourceScanError{"'" + file + "' and '" + candidate.path +
                                             "' are the same resource ('" + logical + "', qualifier '" +
                                             qualifierString(qualifier) + "')"};
        index.addFile(logical, file, qualifier);
    }
    return index;
}

std::string addReswStrings(ResourceIndex& index, std::string_view reswStem, std::string_view language,
                           const std::vector<ReswEntry>& entries)
{
    ResourceQualifier qualifier;
    if (!language.empty())
    {
        const auto parsed = parseQualifiers("lang-" + std::string(language));
        if (!parsed)
            return "'" + std::string(language) + "' is not a language tag";
        qualifier = *parsed;
    }
    for (const auto& entry : entries)
    {
        const std::string logical = std::string(reswStem) + "/" + entry.name;
        const auto normalized = normalizeResourcePath(logical);
        if (entry.name.empty() || entry.name.find_first_of("/\\") != std::string::npos || !normalized ||
            *normalized != logical)
            return "resource name '" + entry.name + "' is not a valid key";
        if (const auto* existing = index.variants(logical))
            for (const auto& candidate : *existing)
                if (candidate.qualifier == qualifier)
                    return "duplicate string '" + logical + "' for language '" + std::string(language) + "'";
        index.addString(logical, entry.value, qualifier);
    }
    return {};
}

} // namespace velora
