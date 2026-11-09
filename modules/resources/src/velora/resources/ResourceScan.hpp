#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <velora/resources/ResourceIndex.hpp>

// Building a resource index from files on disk (R08.4/R08.5).
//
// The one naming rule both the loose provider and `velorac pack` use, so a directory and the package
// built from it resolve every logical path to the same candidate:
//
//   Assets/Logo.scale-200.png            logical Assets/Logo.png, scale 200
//   Assets/Logo.theme-dark_scale-150.png logical Assets/Logo.png, dark theme, scale 150
//   Assets/lang-zh-CN/Banner.png         logical Assets/Banner.png, language zh-CN
//   Assets/scale-200/Icon.png            logical Assets/Icon.png, scale 200
//
// A qualifier is the second-to-last dot segment of a file name or a whole directory name, written in
// the qualifierString spelling. Segments that do not parse as qualifiers are ordinary names; a
// segment that *starts* like a qualifier ("scale-", "lang-", "theme-", "contrast-") but does not
// parse is an error, so a typo is reported instead of becoming a second resource.
//
// .resw files are not indexed here: they are XML, and Velora does not parse XML at run time.
// velorac compiles them into string candidates (addReswStrings).

namespace velora
{

struct ResourceScanError
{
    std::string message;
};

// The logical path and qualifier a package-relative file path stands for, or an error.
[[nodiscard]] std::variant<std::pair<std::string, ResourceQualifier>, ResourceScanError>
logicalResourcePath(std::string_view packagePath);

// Indexes every file under `root` (recursively), skipping .resw files and *.vpri/*.vpak files.
// Two files mapping to the same logical path and qualifier are an error.
[[nodiscard]] std::variant<ResourceIndex, ResourceScanError>
indexResourceFiles(const std::filesystem::path& root, std::string name, std::string defaultLanguage);

// One .resw's entries as string candidates in `language`: logical path "<reswStem>/<name>"
// ("Resources/Greeting"). velorac takes the language from the .resw's directory, as UWP projects
// lay them out ("Strings/zh-CN/Resources.resw"). Returns an error for an entry name that is not a
// valid path segment or a duplicate (logical path, language); an empty string on success.
struct ReswEntry
{
    std::string name;
    std::string value;
};
[[nodiscard]] std::string addReswStrings(ResourceIndex& index, std::string_view reswStem,
                                         std::string_view language, const std::vector<ReswEntry>& entries);

} // namespace velora
