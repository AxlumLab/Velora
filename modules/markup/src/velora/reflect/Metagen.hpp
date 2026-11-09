#pragma once

#include "velora/reflect/Compiler.hpp"
#include "velora/reflect/Metadata.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Build-time metadata generation (`velorac reflect`). Links the reflection compiler; runtime
// code only needs velora/reflect/Metadata.hpp.
namespace velora
{

struct Generated
{
    MetadataResult result;                           // valid, text, document, error
    std::vector<std::filesystem::path> dependencies; // included files read while parsing
};

MetadataDocument generate(const RkAst& ast);
// Parses with parse (the default frontend). Errors carry "source:line:column: message".
Generated generateFromSource(std::string_view source, std::string_view sourceName,
                             const RkParseOptions& options = {});
// C++ that embeds the document and registers it with registerUnit at startup.
// When `include` names the reflected header (as it is included by consumers), the file also
// includes it and staticAsserts that every public field, property accessor and event adder
// exists with a usable signature, so a stale or wrong annotation fails the build, and it
// registers runtime accessors (velora/reflect/Accessors.hpp) for every reflected class.
std::string generateReflectCpp(const MetadataDocument& document, std::string_view unit,
                               std::string_view include = {});

} // namespace velora
