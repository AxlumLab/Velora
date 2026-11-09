#pragma once

// Internal: markup extension and x:Bind sub-parsers.

#include "Source.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace velora
{

struct MarkupContext
{
    const SourceMap& source;
    // Namespace URI (and condition) bound to a prefix in scope; nullopt when undeclared.
    std::function<std::optional<QName>(std::string_view prefix)> resolvePrefix;
    int maxDepth = 16;
    std::vector<XamlDiagnostic>& diagnostics;

    void error(std::size_t begin, std::size_t end, std::string message);
    Span span(const MappedText& text, std::size_t begin, std::size_t end) const
    {
        return source.span(text.sourceOffset(begin), text.sourceOffset(end));
    }
};

// Parses an attribute value that starts with '{' (after the {} escape check).
std::shared_ptr<const MarkupExtension> parseMarkup(const MappedText& value, MarkupContext& context);
std::optional<BindExpression> parseBind(const MappedText& text, MarkupContext& context);

} // namespace velora
