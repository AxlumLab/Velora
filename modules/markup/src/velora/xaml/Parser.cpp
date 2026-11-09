#include "velora/xaml/Parser.hpp"

#include "VeloraXmlLexer.h"
#include "VeloraXmlParser.h"
#include "Markup.hpp"
#include "Source.hpp"

#include <algorithm>
#include <antlr4-runtime.h>
#include <cctype>
#include <map>
#include <set>

namespace velora
{
namespace
{

using X = VeloraXmlParser;

constexpr std::size_t kMaxDiagnostics = 64;

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
bool allSpace(std::string_view s)
{
    return std::ranges::all_of(s, isSpace);
}

// XML end-of-line handling: CRLF and lone CR become LF.
std::string normalizeNewlines(std::string_view s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\r')
        {
            out.push_back('\n');
            if (i + 1 < s.size() && s[i + 1] == '\n')
                ++i;
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string written(const QName& q)
{
    return q.prefix.empty() ? q.local : q.prefix + ":" + q.local;
}

std::pair<std::string, std::string> splitPrefix(std::string_view name)
{
    const auto colon = name.find(':');
    if (colon == std::string_view::npos)
        return {"", std::string(name)};
    return {std::string(name.substr(0, colon)), std::string(name.substr(colon + 1))};
}

// Reports XML syntax errors (lexer and parser) at source positions.
class XmlListener final : public antlr4::BaseErrorListener
{
public:
    XmlListener(const SourceMap& map, std::string_view text, std::vector<XamlDiagnostic>& out)
        : m_map(map), m_text(text), m_out(out)
    {
    }
    void syntaxError(antlr4::Recognizer*, antlr4::Token* token, size_t line, size_t column,
                     const std::string& message, std::exception_ptr) override
    {
        if (m_out.size() >= kMaxDiagnostics)
            return;
        std::size_t begin = 0, end = 0;
        if (token && token->getType() == antlr4::Token::EOF)
        {
            begin = end = m_text.size();
        }
        else if (token)
        {
            begin = m_map.byteOfCodePoint(token->getStartIndex());
            end = m_map.byteOfCodePoint(token->getStopIndex() + 1);
        }
        else
        {
            begin = offsetOf(line, column);
            end = std::min(begin + 1, m_text.size());
        }
        std::string text = message;
        if (const auto at = text.find(" expecting {");
            at != std::string::npos && std::ranges::count(text, ',') > 4)
            text.erase(at);
        m_out.push_back({m_map.span(begin, end), text});
    }

private:
    std::size_t offsetOf(std::size_t line, std::size_t column) const
    {
        std::size_t i = 0;
        for (std::size_t l = 1; l < line && i < m_text.size(); ++i)
            if (m_text[i] == '\n')
                ++l;
        for (std::size_t c = 0; c < column && i < m_text.size(); ++c)
        {
            ++i;
            while (i < m_text.size() && (static_cast<unsigned char>(m_text[i]) & 0xC0) == 0x80)
                ++i;
        }
        return i;
    }
    const SourceMap& m_map;
    std::string_view m_text;
    std::vector<XamlDiagnostic>& m_out;
};

struct Scope
{
    std::map<std::string, QName, std::less<>> prefixes; // prefix -> namespace (ns, condition)
    std::set<std::string, std::less<>> ignorable;       // ignorable namespace URIs
    bool preserveSpace = false;
};

QName namespaceOf(std::string_view uri)
{
    QName out;
    if (uri.starts_with(kXamlPresentationNs) && uri.size() > kXamlPresentationNs.size() &&
        uri[kXamlPresentationNs.size()] == '?')
    {
        out.ns = std::string(kXamlPresentationNs);
        out.condition = std::string(uri.substr(kXamlPresentationNs.size() + 1));
    }
    else
    {
        out.ns = std::string(uri);
    }
    return out;
}

bool supportedNamespace(std::string_view uri)
{
    return uri == kXamlPresentationNs || uri == kXamlNs || uri == kXamlDesignNs ||
           uri == kXamlCompatibilityNs || (uri.starts_with("using:") && uri.size() > 6);
}

class Builder
{
public:
    Builder(const SourceMap& map, std::string_view text, const XamlParseOptions& options,
            std::vector<XamlDiagnostic>& diagnostics)
        : m_map(map), m_text(text), m_options(options), m_diagnostics(diagnostics)
    {
    }

    void document(X::DocumentContext* ctx, XamlDocument& out);

private:
    std::size_t beginOf(antlr4::Token* t) const
    {
        return m_map.byteOfCodePoint(t->getStartIndex());
    }
    std::size_t endOf(antlr4::Token* t) const
    {
        return m_map.byteOfCodePoint(t->getStopIndex() + 1);
    }
    Span span(std::size_t b, std::size_t e) const
    {
        return m_map.span(b, e);
    }
    Span span(antlr4::Token* t) const
    {
        return span(beginOf(t), endOf(t));
    }
    void error(std::size_t b, std::size_t e, std::string message)
    {
        if (m_diagnostics.size() < kMaxDiagnostics)
            m_diagnostics.push_back({span(b, e), std::move(message)});
    }
    void error(antlr4::Token* t, std::string message)
    {
        error(beginOf(t), endOf(t), std::move(message));
    }
    MappedText mapped(std::size_t b, std::size_t e) const
    {
        return MappedText::of(m_text.substr(b, e - b), b);
    }

    std::shared_ptr<XamlElement> element(X::ElementContext* ctx, const Scope& outer,
                                         const XamlElement* parent, int depth);
    void declareNamespaces(X::ElementContext* ctx, Scope& scope);
    void attribute(X::AttributeContext* ctx, const Scope& scope, XamlElement& element, bool root,
                   std::set<std::string>& seen);
    void content(antlr4::tree::TerminalNode* node, XamlElement& element);
    void comment(antlr4::Token* token, std::vector<XamlComment>& out);
    std::optional<MappedText> decoded(antlr4::Token* token, std::size_t trim, DecodeMode mode);
    void normalizeWhitespace(XamlElement& element) const;
    bool knownType(const QName& name) const
    {
        return m_options.isKnownType ? m_options.isKnownType(name) : isBuiltinType(name);
    }

    const SourceMap& m_map;
    std::string_view m_text;
    const XamlParseOptions& m_options;
    std::vector<XamlDiagnostic>& m_diagnostics;
};

std::optional<MappedText> Builder::decoded(antlr4::Token* token, std::size_t trim, DecodeMode mode)
{
    const std::size_t b = beginOf(token) + trim, e = endOf(token) - trim;
    auto result = decode(mapped(b, e), mode);
    if (auto* failure = std::get_if<DecodeError>(&result))
    {
        const auto semicolon = m_text.find(';', failure->offset);
        error(failure->offset,
              semicolon == std::string_view::npos || semicolon > e ? failure->offset + 1 : semicolon + 1,
              failure->message);
        return std::nullopt;
    }
    return std::get<MappedText>(std::move(result));
}

void Builder::comment(antlr4::Token* token, std::vector<XamlComment>& out)
{
    const std::size_t b = beginOf(token), e = endOf(token);
    const std::string_view body = m_text.substr(b + 4, e - b - 7);
    if (const auto dash = body.find("--"); dash != std::string_view::npos || body.ends_with('-'))
    {
        const std::size_t at = b + 4 + (dash == std::string_view::npos ? body.size() - 1 : dash);
        error(at, at + 2, "'--' is not allowed inside a comment");
    }
    out.push_back({normalizeNewlines(body), span(b, e)});
}

void Builder::document(X::DocumentContext* ctx, XamlDocument& out)
{
    bool seenRoot = false;
    for (auto* child : ctx->children)
    {
        if (auto* e = dynamic_cast<X::ElementContext*>(child))
        {
            Scope scope;
            scope.prefixes["xml"] = QName{"xml", "", std::string(kXmlNs), ""};
            out.root = element(e, scope, nullptr, 1);
            seenRoot = true;
            continue;
        }
        auto* misc = dynamic_cast<X::MiscContext*>(child);
        if (!misc)
            continue;
        auto* t = dynamic_cast<antlr4::tree::TerminalNode*>(misc->children.front())->getSymbol();
        switch (t->getType())
        {
        case X::XML_TEXT:
            if (!allSpace(t->getText()))
            {
                const std::string s = t->getText();
                const std::size_t first = beginOf(t) + s.find_first_not_of(" \t\r\n");
                error(first, first + 1, "text is not allowed outside the root element");
            }
            break;
        case X::XML_COMMENT:
            comment(t, out.comments);
            break;
        case X::XML_DOCTYPE:
            error(t, "DOCTYPE is not allowed (DTDs and external entities are not supported)");
            break;
        case X::XML_PI:
        {
            const std::string pi = t->getText();
            if (!pi.starts_with("<?xml ") && pi != "<?xml?>")
            {
                error(t, "processing instructions are not supported");
            }
            else if (beginOf(t) != 0 || seenRoot)
            {
                error(t, "the XML declaration must be at the very start of the file");
            }
            else
            {
                // Only UTF-8 is accepted; anything else would have been rejected as invalid UTF-8 or a BOM.
                const auto at = pi.find("encoding=");
                if (at != std::string::npos)
                {
                    std::string encoding = pi.substr(at + 10, pi.find_first_of("\"'", at + 10) - at - 10);
                    std::ranges::transform(encoding, encoding.begin(), [](unsigned char c)
                                           { return static_cast<char>(std::tolower(c)); });
                    if (encoding != "utf-8")
                        error(t, "unsupported encoding '" + encoding + "' (XAML sources must be UTF-8)");
                    out.encoding = encoding;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

void Builder::declareNamespaces(X::ElementContext* ctx, Scope& scope)
{
    for (auto* a : ctx->attribute())
    {
        const std::string name = a->XML_NAME()->getText();
        if (name != "xmlns" && !name.starts_with("xmlns:"))
            continue;
        auto value = decoded(a->XML_STRING()->getSymbol(), 1, DecodeMode::kAttribute);
        if (!value)
            continue;
        const std::string prefix = name == "xmlns" ? "" : name.substr(6);
        if (value->text.empty() && prefix.empty() && m_options.plainXml)
        {
            scope.prefixes.insert_or_assign(std::string(), QName{}); // xmlns="": back to no namespace
            continue;
        }
        if (value->text.empty())
        {
            error(a->XML_STRING()->getSymbol(), "empty namespace URI for '" + name + "'");
            continue;
        }
        if (prefix == "xml" || prefix == "xmlns")
        {
            error(a->XML_NAME()->getSymbol(), "the '" + prefix + "' prefix cannot be redeclared");
            continue;
        }
        QName binding = namespaceOf(value->text);
        binding.prefix = prefix;
        scope.prefixes[prefix] = binding;
    }
    // mc:Ignorable lists prefixes whose namespaces are design-time only.
    for (auto* a : ctx->attribute())
    {
        const auto [prefix, local] = splitPrefix(a->XML_NAME()->getText());
        const auto mc = scope.prefixes.find(prefix);
        if (prefix.empty() || mc == scope.prefixes.end() || mc->second.ns != kXamlCompatibilityNs ||
            local != "Ignorable")
            continue;
        auto value = decoded(a->XML_STRING()->getSymbol(), 1, DecodeMode::kAttribute);
        if (!value)
            continue;
        std::size_t i = 0;
        while (i < value->text.size())
        {
            while (i < value->text.size() && value->text[i] == ' ')
                ++i;
            const std::size_t start = i;
            while (i < value->text.size() && value->text[i] != ' ')
                ++i;
            if (start == i)
                break;
            const std::string listed = value->text.substr(start, i - start);
            const auto declared = scope.prefixes.find(listed);
            if (declared == scope.prefixes.end())
                error(value->sourceOffset(start), value->sourceOffset(i),
                      "mc:Ignorable lists undeclared prefix '" + listed + "'");
            else
                scope.ignorable.insert(declared->second.ns);
        }
    }
    for (auto* a : ctx->attribute())
    {
        const std::string name = a->XML_NAME()->getText();
        if (name != "xmlns" && !name.starts_with("xmlns:"))
            continue;
        const auto it = scope.prefixes.find(name == "xmlns" ? "" : name.substr(6));
        if (it == scope.prefixes.end())
            continue;
        if (!m_options.plainXml && !supportedNamespace(it->second.ns) && !scope.ignorable.contains(it->second.ns))
            error(a->XML_STRING()->getSymbol(), "unsupported XAML namespace '" + it->second.ns + "'");
    }
}

std::shared_ptr<XamlElement> Builder::element(X::ElementContext* ctx, const Scope& outer,
                                              const XamlElement* parent, int depth)
{
    auto out = std::make_shared<XamlElement>();
    XamlElement& e = *out;
    auto* open = ctx->XML_OPEN()->getSymbol();
    auto* nameToken = ctx->XML_NAME(0)->getSymbol();
    auto* last = ctx->getStop();
    e.span = span(beginOf(open), endOf(last));
    e.nameSpan = span(nameToken);
    const std::string name = nameToken->getText();
    if (ctx->XML_NAME().size() > 1 && ctx->XML_NAME(1)->getText() != name)
    {
        auto* close = ctx->XML_NAME(1)->getSymbol();
        const auto at = m_map.at(beginOf(nameToken));
        error(close, "closing tag '" + close->getText() + "' does not match '" + name + "' opened at " +
                         std::to_string(at.line) + ":" + std::to_string(at.column));
    }

    Scope scope = outer;
    declareNamespaces(ctx, scope);
    auto [prefix, local] = splitPrefix(name);
    e.name.prefix = prefix;
    e.name.local = local;
    auto bound = scope.prefixes.find(prefix);
    if (bound == scope.prefixes.end() && prefix.empty() && m_options.plainXml)
        bound = scope.prefixes.emplace(std::string(), QName{}).first; // plain XML: no namespace
    if (bound == scope.prefixes.end())
    {
        error(nameToken, prefix.empty() ? "no default namespace is declared for element '" + name + "'"
                                        : "undeclared namespace prefix '" + prefix + "'");
        return out;
    }
    e.name.ns = bound->second.ns;
    e.name.condition = bound->second.condition;
    if (scope.ignorable.contains(e.name.ns))
    {
        e.ignored = true; // design-time content is kept as a leaf and never validated
        return out;
    }

    const bool objectParent = parent && !parent->propertyElement;
    if (const auto dot = local.rfind('.'); dot != std::string::npos)
    {
        e.propertyElement = true;
        e.owner = local.substr(0, dot);
        e.property = local.substr(dot + 1);
        QName owner{e.name.prefix, e.owner, e.name.ns, e.name.condition};
        if (e.owner.empty() || e.property.empty())
            error(nameToken, "malformed property element '" + name + "'");
        else if (!objectParent)
            error(nameToken, parent
                                 ? "property element '" + name + "' cannot be nested in property element '" +
                                       written(parent->name) + "'"
                                 : "property element '" + name + "' must be a child of an object element");
        else if (e.owner != parent->name.local && !knownType(owner))
            error(nameToken, "property element owner '" + e.owner + "' is neither the parent type '" +
                                 parent->name.local + "' nor a known type");
    }
    else if (!knownType(e.name))
    {
        error(nameToken, "unknown element type '" + name + "'");
    }

    scope.preserveSpace = outer.preserveSpace;
    std::set<std::string> seen;
    for (auto* a : ctx->attribute())
        attribute(a, scope, e, parent == nullptr, seen);
    e.preserveSpace = scope.preserveSpace;
    for (const auto& a : e.attributes)
        if (a.kind == XamlAttributeKind::kXmlAttribute && a.name.local == "space")
            e.preserveSpace = scope.preserveSpace = a.value == "preserve";
    if (e.propertyElement)
    {
        for (const auto& a : e.attributes)
            if (a.kind != XamlAttributeKind::kNamespaceDeclaration && a.kind != XamlAttributeKind::kIgnored)
                m_diagnostics.push_back({a.span, "property element '" + name + "' cannot have attributes"});
    }

    for (auto* c : ctx->content())
    {
        if (auto* child = c->element())
        {
            if (depth + 1 > m_options.maxDepth)
            {
                error(child->XML_NAME(0)->getSymbol(),
                      "elements nested deeper than " + std::to_string(m_options.maxDepth));
                continue;
            }
            e.children.emplace_back(element(child, scope, &e, depth + 1));
        }
        else
        {
            content(dynamic_cast<antlr4::tree::TerminalNode*>(c->children.front()), e);
        }
    }
    normalizeWhitespace(e);
    return out;
}

void Builder::attribute(X::AttributeContext* ctx, const Scope& scope, XamlElement& element, bool root,
                        std::set<std::string>& seen)
{
    auto* nameToken = ctx->XML_NAME()->getSymbol();
    auto* valueToken = ctx->XML_STRING()->getSymbol();
    XamlAttribute a;
    const std::string name = nameToken->getText();
    auto [prefix, local] = splitPrefix(name);
    a.name.prefix = prefix;
    a.name.local = local;
    a.span = span(beginOf(nameToken), endOf(valueToken));
    a.valueSpan = span(beginOf(valueToken) + 1, endOf(valueToken) - 1);
    a.raw = std::string(m_text.substr(beginOf(valueToken) + 1, endOf(valueToken) - beginOf(valueToken) - 2));
    auto value = decoded(valueToken, 1, DecodeMode::kAttribute);
    if (!value)
        return;
    a.value = value->text;

    if (name == "xmlns" || prefix == "xmlns")
    {
        a.kind = XamlAttributeKind::kNamespaceDeclaration;
        a.name.ns = "xmlns";
    }
    else if (!prefix.empty())
    {
        const auto bound = scope.prefixes.find(prefix);
        if (bound == scope.prefixes.end())
        {
            error(nameToken, "undeclared namespace prefix '" + prefix + "'");
            return;
        }
        a.name.ns = bound->second.ns;
        a.name.condition = bound->second.condition;
        if (scope.ignorable.contains(a.name.ns))
        {
            a.kind = XamlAttributeKind::kIgnored;
        }
        else if (a.name.ns == kXmlNs)
        {
            a.kind = XamlAttributeKind::kXmlAttribute;
            if (local != "space" && local != "lang")
                error(nameToken, "unsupported attribute 'xml:" + local + "'");
            else if (local == "space" && a.value != "preserve" && a.value != "default")
                error(valueToken, "xml:space must be 'preserve' or 'default'");
        }
        else if (a.name.ns == kXamlNs)
        {
            a.kind = XamlAttributeKind::kDirective;
            static const std::set<std::string_view> directives = {
                "Class",           "Name",          "Key",     "Uid", "Load", "DeferLoadStrategy", "Phase",
                "DefaultBindMode", "FieldModifier", "DataType"};
            if (!directives.contains(local))
                error(nameToken, "unknown XAML directive 'x:" + local + "'");
            else if (local == "Class" && !root)
                error(nameToken, "x:Class is only allowed on the root element");
        }
        else if (a.name.ns == kXamlCompatibilityNs)
        {
            a.kind = XamlAttributeKind::kDirective;
            if (local != "Ignorable")
                error(nameToken, "unsupported markup-compatibility attribute 'mc:" + local + "'");
        }
        else if (a.name.ns == kXamlPresentationNs || a.name.ns.starts_with("using:"))
        {
            a.kind = XamlAttributeKind::kProperty;
        }
        else
        {
            error(nameToken, "attribute '" + name + "' is in an unsupported namespace");
            return;
        }
    }
    if (a.kind == XamlAttributeKind::kProperty)
    {
        if (const auto dot = local.rfind('.'); dot != std::string::npos)
        {
            a.kind = XamlAttributeKind::kAttachedProperty;
            a.owner = local.substr(0, dot);
            QName owner{prefix, a.owner, a.name.ns.empty() ? element.name.ns : a.name.ns, a.name.condition};
            if (a.owner.empty() || dot + 1 == local.size())
                error(nameToken, "malformed attached property '" + name + "'");
            else if (a.owner != element.name.local && !knownType(owner))
                error(nameToken, "attached property owner '" + a.owner + "' is not a known type");
        }
    }
    const std::string key = a.name.ns + "|" + a.name.condition + "|" + local;
    if (!seen.insert(key).second)
    {
        error(nameToken, "duplicate attribute '" + name + "'");
        return;
    }
    const bool mayBeMarkup =
        a.kind == XamlAttributeKind::kProperty || a.kind == XamlAttributeKind::kAttachedProperty;
    if (mayBeMarkup && a.value.starts_with("{}"))
    {
        a.value.erase(0, 2); // {} escapes a literal brace at the start
    }
    else if (mayBeMarkup && a.value.starts_with("{"))
    {
        MarkupContext context{m_map,
                              [&](std::string_view p) -> std::optional<QName>
                              {
                                  const auto it = scope.prefixes.find(p);
                                  if (it == scope.prefixes.end())
                                      return std::nullopt;
                                  return it->second;
                              },
                              m_options.maxMarkupDepth, m_diagnostics};
        a.markup = parseMarkup(*value, context);
    }
    element.attributes.push_back(std::move(a));
}

void Builder::content(antlr4::tree::TerminalNode* node, XamlElement& element)
{
    auto* t = node->getSymbol();
    switch (t->getType())
    {
    case X::XML_TEXT:
    {
        auto value = decoded(t, 0, DecodeMode::kText);
        if (!value)
            return;
        XamlText text{t->getText(), value->text, false, span(t)};
        element.children.emplace_back(std::move(text));
        break;
    }
    case X::XML_CDATA:
    {
        const std::size_t b = beginOf(t), e = endOf(t);
        std::string body = normalizeNewlines(m_text.substr(b + 9, e - b - 12));
        element.children.emplace_back(XamlText{body, body, true, span(b, e)});
        break;
    }
    case X::XML_COMMENT:
    {
        std::vector<XamlComment> one;
        comment(t, one);
        element.children.emplace_back(std::move(one.front()));
        break;
    }
    case X::XML_PI:
        error(t, "processing instructions are not supported");
        break;
    case X::XML_DOCTYPE:
        error(t, "DOCTYPE is not allowed (DTDs and external entities are not supported)");
        break;
    default:
        break;
    }
}

// XAML whitespace: unless xml:space="preserve", runs of whitespace collapse to one space,
// whitespace at the start and end of an element's content is removed, and whitespace-only
// text between elements disappears. CDATA is kept as written.
void Builder::normalizeWhitespace(XamlElement& element) const
{
    if (element.preserveSpace)
        return;
    const auto isContent = [](const XamlNode& n) { return !std::holds_alternative<XamlComment>(n); };
    std::vector<XamlNode> kept;
    for (std::size_t i = 0; i < element.children.size(); ++i)
    {
        auto* text = std::get_if<XamlText>(&element.children[i]);
        if (!text || text->cdata)
        {
            kept.push_back(std::move(element.children[i]));
            continue;
        }
        std::string collapsed;
        for (char c : text->value)
        {
            if (isSpace(c))
            {
                if (collapsed.empty() || collapsed.back() != ' ')
                    collapsed.push_back(' ');
            }
            else
            {
                collapsed.push_back(c);
            }
        }
        const bool first = std::none_of(element.children.begin(),
                                        element.children.begin() + static_cast<std::ptrdiff_t>(i), isContent);
        const bool last = std::none_of(element.children.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                       element.children.end(), isContent);
        if (first && collapsed.starts_with(' '))
            collapsed.erase(0, 1);
        if (last && collapsed.ends_with(' '))
            collapsed.pop_back();
        if (collapsed.empty() || collapsed == " ")
            continue;
        text->value = std::move(collapsed);
        kept.push_back(std::move(element.children[i]));
    }
    element.children = std::move(kept);
}

} // namespace

const XamlAttribute* XamlElement::attribute(std::string_view ns, std::string_view local) const
{
    for (const auto& a : attributes)
        if (a.name.local == local && (a.name.ns == ns || (ns.empty() && a.name.prefix.empty())))
            return &a;
    return nullptr;
}

XamlParseResult parseXaml(std::string_view source, const XamlParseOptions& options)
{
    XamlParseResult result;
    auto& diagnostics = result.diagnostics;
    if (source.size() > options.maxBytes)
    {
        diagnostics.push_back(
            {{{1, 1, 0}, {1, 1, 0}}, "source is larger than " + std::to_string(options.maxBytes) + " bytes"});
        return result;
    }
    if (source.starts_with("\xFF\xFE") || source.starts_with("\xFE\xFF"))
    {
        diagnostics.push_back(
            {{{1, 1, 0}, {1, 1, 2}}, "UTF-16 sources are not supported; save the file as UTF-8"});
        return result;
    }
    if (source.starts_with("\xEF\xBB\xBF"))
        source.remove_prefix(3);
    const SourceMap map(source);
    if (const auto bad = invalidUtf8(source))
    {
        diagnostics.push_back({map.span(*bad, *bad + 1), "invalid UTF-8 byte sequence"});
        return result;
    }

    antlr4::ANTLRInputStream input{std::string(source)};
    VeloraXmlLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    XmlListener listener(map, source, diagnostics);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    tokens.fill();
    if (!diagnostics.empty())
        return result;
    // Reject excessive nesting before the recursive-descent parser sees it.
    int depth = 0;
    for (auto* t : tokens.getTokens())
    {
        if (t->getType() == VeloraXmlLexer::XML_OPEN && ++depth > options.maxDepth)
        {
            const std::size_t b = map.byteOfCodePoint(t->getStartIndex());
            diagnostics.push_back(
                {map.span(b, b + 1), "elements nested deeper than " + std::to_string(options.maxDepth)});
            return result;
        }
        if (t->getType() == VeloraXmlLexer::XML_OPEN_END || t->getType() == VeloraXmlLexer::XML_EMPTY_CLOSE)
            --depth;
    }

    VeloraXmlParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);
    auto* tree = parser.document();
    if (!diagnostics.empty())
        return result;
    Builder(map, source, options, diagnostics).document(tree, result.document);
    std::ranges::stable_sort(diagnostics, [](const XamlDiagnostic& a, const XamlDiagnostic& b)
                             { return a.span.begin.offset < b.span.begin.offset; });
    return result;
}

std::string formatDiagnostic(const XamlDiagnostic& d, std::string_view sourceName)
{
    return std::string(sourceName) + ":" + std::to_string(d.span.begin.line) + ":" +
           std::to_string(d.span.begin.column) + ": error: " + d.message;
}

} // namespace velora
