#pragma once

#include "velora/xaml/Ast.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Build-time XAML front end: ANTLR4-generated XML, markup-extension and x:Bind parsers
// (tools/xaml/grammar) plus a semantic pass. Accepts the Velora XAML subset only; it is not
// a complete WinUI XAML implementation.
namespace velora
{

struct XamlParseOptions
{
    std::size_t maxBytes = 4u << 20;
    int maxDepth = 128;      // element nesting
    int maxMarkupDepth = 16; // markup extension nesting
    // Whether an object element type exists. Defaults to isBuiltinType.
    std::function<bool(const QName&)> isKnownType;
    // Accept unprefixed elements with no default namespace (plain XML such as .resw files).
    bool plainXml = false;
};

struct XamlParseResult
{
    XamlDocument document;
    std::vector<XamlDiagnostic> diagnostics;
    bool ok() const noexcept
    {
        return diagnostics.empty();
    }
};

XamlParseResult parseXaml(std::string_view source, const XamlParseOptions& options = {});

// The supported WinUI presentation types, x: intrinsic types, and any type in a using:
// namespace (resolved later against reflection metadata).
bool isBuiltinType(const QName& name);

// Stable textual form of a document with source spans, used for golden tests.
std::string dumpXaml(const XamlDocument& document);
// "<source>:<line>:<column>: error: <message>"
std::string formatDiagnostic(const XamlDiagnostic& diagnostic, std::string_view sourceName);

} // namespace velora
