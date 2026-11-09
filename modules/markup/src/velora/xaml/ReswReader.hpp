#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <velora/resources/ResourceIndex.hpp>
#include <velora/resources/ResourceScan.hpp>
#include <velora/xaml/Ast.hpp>

// Build-time resource compilation for velorac (R08.4/R08.6): .resw string tables and whole resource
// trees into a .vpri index. Applications never parse .resw: velorac turns each into string
// candidates of the package's index, and the run time reads only the index.
//
// A .resw is the standard UWP resource table: <root> with <data name="Key"><value>Text</value></data>
// entries. Only string entries are supported; a <data> with a `type` (embedded binary or a .NET
// type) is reported as a diagnostic, never dropped.

namespace velora
{

struct ReswResult
{
    std::vector<ReswEntry> entries;
    std::vector<XamlDiagnostic> diagnostics;
    bool ok() const noexcept
    {
        return diagnostics.empty();
    }
};

[[nodiscard]] ReswResult readResw(std::string_view source);

// Indexes the resource tree at `root` for a package: every file by the ResourceScan naming rule, plus
// every .resw at "<dir>/<language>/<stem>.resw" (the UWP layout; the language directory may also be
// spelled "lang-<tag>") as strings "<stem>/<key>" in that language. A .resw elsewhere is an error.
// Diagnostics name the file; the index is returned only when there are none.
[[nodiscard]] std::variant<ResourceIndex, std::string>
buildResourceIndex(const std::filesystem::path& root, std::string name, std::string defaultLanguage);

} // namespace velora
