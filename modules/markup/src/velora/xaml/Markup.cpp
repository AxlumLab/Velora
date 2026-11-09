#include "Markup.hpp"

#include "VeloraBindLexer.h"
#include "VeloraBindParser.h"
#include "VeloraMarkupLexer.h"
#include "VeloraMarkupParser.h"

#include <algorithm>
#include <antlr4-runtime.h>
#include <set>
#include <velora/globals.hpp>

namespace velora
{

const MarkupArgument* MarkupExtension::argument(std::string_view name) const
{
    for (const auto& a : named)
        if (a.name == name)
            return &a;
    return nullptr;
}

void MarkupContext::error(std::size_t begin, std::size_t end, std::string message)
{
    diagnostics.push_back({source.span(begin, end), std::move(message)});
}

namespace
{

using M = VeloraMarkupParser;
using B = VeloraBindParser;

// Byte offset in `text` of a (1-based line, 0-based code point column) pair from an ANTLR lexer.
std::size_t byteAt(std::string_view text, std::size_t line, std::size_t column)
{
    std::size_t i = 0;
    for (std::size_t l = 1; l < line && i < text.size(); ++i)
        if (text[i] == '\n')
            ++l;
    for (std::size_t c = 0; c < column && i < text.size(); ++c)
    {
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
            ++i;
    }
    return i;
}

// Reports the first syntax error of a sub-parser at its source position; later ones cascade.
class Listener final : public antlr4::BaseErrorListener
{
public:
    Listener(MarkupContext& context, const MappedText& text, const std::vector<u32>& cps, std::string prefix)
        : m_context(context), m_text(text), m_cps(cps), m_prefix(std::move(prefix))
    {
    }
    void syntaxError(antlr4::Recognizer*, antlr4::Token* token, size_t line, size_t column,
                     const std::string& message, std::exception_ptr) override
    {
        if (m_failed)
            return;
        m_failed = true;
        std::size_t begin = 0, end = 0;
        if (token && token->getType() == antlr4::Token::EOF)
        {
            begin = end = m_text.text.size();
        }
        else if (token)
        {
            begin = m_cps[std::min<std::size_t>(token->getStartIndex(), m_cps.size() - 1)];
            end = m_cps[std::min<std::size_t>(token->getStopIndex() + 1, m_cps.size() - 1)];
        }
        else
        {
            begin = byteAt(m_text.text, line, column);
            end = std::min(begin + 1, m_text.text.size());
        }
        std::string text = message;
        if (const auto at = text.find(" expecting {");
            at != std::string::npos && std::ranges::count(text, ',') > 4)
            text.erase(at);
        m_context.error(m_text.sourceOffset(begin), m_text.sourceOffset(end), m_prefix + text);
    }
    bool failed() const
    {
        return m_failed;
    }

private:
    MarkupContext& m_context;
    const MappedText& m_text;
    const std::vector<u32>& m_cps;
    std::string m_prefix;
    bool m_failed = false;
};

void collectTokens(antlr4::tree::ParseTree* tree, std::vector<antlr4::Token*>& out)
{
    if (auto* t = dynamic_cast<antlr4::tree::TerminalNode*>(tree))
    {
        out.push_back(t->getSymbol());
        return;
    }
    for (auto* c : tree->children)
        collectTokens(c, out);
}

std::string unescape(std::string_view text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size())
            ++i;
        out.push_back(text[i]);
    }
    return out;
}

// Positions inside a sub-parsed text: ANTLR indices are code points of that text.
struct Cursor
{
    MarkupContext& context;
    const MappedText& text;
    std::vector<u32> cps;

    std::size_t begin(antlr4::Token* t) const
    {
        return cps[t->getStartIndex()];
    }
    std::size_t end(antlr4::Token* t) const
    {
        return cps[t->getStopIndex() + 1];
    }
    std::size_t begin(antlr4::ParserRuleContext* c) const
    {
        return begin(c->getStart());
    }
    std::size_t end(antlr4::ParserRuleContext* c) const
    {
        return end(c->getStop());
    }
    Span span(std::size_t b, std::size_t e) const
    {
        return context.span(text, b, e);
    }
    void error(std::size_t b, std::size_t e, std::string message) const
    {
        context.error(text.sourceOffset(b), text.sourceOffset(e), std::move(message));
    }
};

struct Kind
{
    std::string_view name;
    bool needsMain;                // the positional argument (or its named form) is required
    std::string_view positionalAs; // named form of the positional argument
    std::set<std::string_view> named;
};

const Kind* findKind(std::string_view canonical)
{
    static const Kind kinds[] = {
        {"StaticResource", true, "ResourceKey", {"ResourceKey"}},
        {"ThemeResource", true, "ResourceKey", {"ResourceKey"}},
        {"Binding",
         false,
         "Path",
         {"Path", "Mode", "Converter", "ConverterParameter", "ConverterLanguage", "ElementName",
          "RelativeSource", "Source", "FallbackValue", "TargetNullValue", "UpdateSourceTrigger"}},
        {"x:Bind",
         true,
         "Path",
         {"Path", "Mode", "Converter", "ConverterParameter", "ConverterLanguage", "FallbackValue",
          "TargetNullValue", "UpdateSourceTrigger", "BindBack"}},
        {"TemplateBinding", true, "Property", {"Converter", "ConverterParameter"}},
        {"RelativeSource", true, "Mode", {"Mode"}},
        {"x:Null", false, "", {}},
    };
    for (const auto& k : kinds)
        if (k.name == canonical)
            return &k;
    return nullptr;
}

class MarkupBuilder
{
public:
    explicit MarkupBuilder(Cursor cursor) : m_at(std::move(cursor)) {}
    std::shared_ptr<const MarkupExtension> extension(M::ExtensionContext* ctx, int depth, bool inBinding);

private:
    std::string scalarText(antlr4::ParserRuleContext* ctx) const;
    MarkupValue value(M::ValueContext* ctx, int depth, std::string_view argument, bool binding);
    void checkChoice(const MarkupExtension& ext, std::string_view argument,
                     std::initializer_list<std::string_view> allowed);

    Cursor m_at;
};

// Scalar spelling: quotes and backslash escapes removed, inner whitespace kept as written.
// Parenthesized groups (x:Bind function arguments) are kept verbatim.
std::string MarkupBuilder::scalarText(antlr4::ParserRuleContext* ctx) const
{
    std::vector<antlr4::Token*> tokens;
    collectTokens(ctx, tokens);
    std::string out;
    std::size_t previous = std::string::npos;
    int parens = 0;
    for (auto* t : tokens)
    {
        const std::size_t b = m_at.begin(t), e = m_at.end(t);
        if (previous != std::string::npos)
            out += m_at.text.text.substr(previous, b - previous);
        const std::string_view spelling = std::string_view(m_at.text.text).substr(b, e - b);
        if (t->getType() == M::LPAREN)
            ++parens;
        else if (t->getType() == M::RPAREN)
            --parens;
        if (parens == 0 && t->getType() == M::STRING)
            out += unescape(spelling.substr(1, spelling.size() - 2));
        else if (parens == 0 && t->getType() == M::ESCAPED)
            out += spelling.substr(1);
        else
            out += spelling;
        previous = e;
    }
    return out;
}

MarkupValue MarkupBuilder::value(M::ValueContext* ctx, int depth, std::string_view argument, bool binding)
{
    MarkupValue out;
    out.span = m_at.span(m_at.begin(ctx), m_at.end(ctx));
    if (auto* nested = ctx->extension())
    {
        static const std::set<std::string_view> scalarOnly = {
            "", "Path", "Mode", "ElementName", "UpdateSourceTrigger", "ResourceKey"};
        if (scalarOnly.contains(argument))
            m_at.error(m_at.begin(nested), m_at.end(nested),
                       argument.empty() ? "a positional argument cannot be a markup extension"
                                        : "'" + std::string(argument) + "' cannot be a markup extension");
        out.extension = extension(nested, depth + 1, binding && argument == "RelativeSource");
    }
    else
    {
        out.text = scalarText(ctx->scalar());
    }
    return out;
}

void MarkupBuilder::checkChoice(const MarkupExtension& ext, std::string_view argument,
                                std::initializer_list<std::string_view> allowed)
{
    const MarkupArgument* a = ext.argument(argument);
    if (!a && ext.positional && findKind(ext.kind)->positionalAs == argument)
        a = &*ext.positional;
    if (!a || a->value.extension)
        return;
    if (std::ranges::find(allowed, a->value.text) == allowed.end())
        m_at.context.diagnostics.push_back(
            {a->value.span, "unsupported " + std::string(argument) + " '" + a->value.text + "'"});
}

std::shared_ptr<const MarkupExtension> MarkupBuilder::extension(M::ExtensionContext* ctx, int depth,
                                                                bool inBinding)
{
    auto ext = std::make_shared<MarkupExtension>();
    ext->span = m_at.span(m_at.begin(ctx), m_at.end(ctx));
    if (depth > m_at.context.maxDepth)
    {
        m_at.error(m_at.begin(ctx), m_at.end(ctx),
                   "markup extensions nested deeper than " + std::to_string(m_at.context.maxDepth));
        return ext;
    }
    auto* nameToken = ctx->IDENT()->getSymbol();
    const std::size_t nameBegin = m_at.begin(nameToken), nameEnd = m_at.end(nameToken);
    const std::string name = nameToken->getText();
    const auto colon = name.find(':');
    ext->name.prefix = colon == std::string::npos ? "" : name.substr(0, colon);
    ext->name.local = colon == std::string::npos ? name : name.substr(colon + 1);
    const auto ns = m_at.context.resolvePrefix(ext->name.prefix);
    if (!ns)
    {
        m_at.error(nameBegin, nameEnd, "undeclared namespace prefix '" + ext->name.prefix + "'");
        return ext;
    }
    ext->name.ns = ns->ns;
    ext->name.condition = ns->condition;
    if (ns->ns == kXamlNs && (ext->name.local == "Bind" || ext->name.local == "Null"))
        ext->kind = "x:" + ext->name.local;
    else if (ns->ns == kXamlPresentationNs)
        ext->kind = ext->name.local;
    const Kind* kind = findKind(ext->kind);
    if (!kind || (ext->kind == "RelativeSource" && !inBinding))
    {
        m_at.error(nameBegin, nameEnd,
                   kind ? "RelativeSource is only supported as Binding.RelativeSource"
                        : "unsupported markup extension '" + name + "'");
        ext->kind.clear();
        return ext;
    }

    const bool binding = ext->kind == "Binding";
    M::ValueContext* mainValue = nullptr;
    if (auto* args = ctx->argumentList())
    {
        if (ext->kind == "x:Null")
        {
            m_at.error(m_at.begin(args), m_at.end(args), "x:Null takes no arguments");
            return ext;
        }
        if (auto* p = args->positional())
        {
            mainValue = p->value();
            ext->positional = MarkupArgument{"", {}, value(mainValue, depth, "", binding)};
        }
        for (auto* n : args->namedArgument())
        {
            auto* id = n->IDENT()->getSymbol();
            const std::string arg = id->getText();
            const std::size_t b = m_at.begin(id), e = m_at.end(id);
            if (!kind->named.contains(arg))
            {
                m_at.error(b, e, "'" + arg + "' is not an argument of " + ext->kind);
                continue;
            }
            if (ext->argument(arg) || (ext->positional && arg == kind->positionalAs))
            {
                m_at.error(b, e, "duplicate argument '" + arg + "'");
                continue;
            }
            if (arg == kind->positionalAs)
                mainValue = n->value();
            ext->named.push_back({arg, m_at.span(b, e), value(n->value(), depth, arg, binding)});
        }
    }
    const MarkupArgument* main = ext->positional ? &*ext->positional : ext->argument(kind->positionalAs);
    if (kind->needsMain && (!main || (!main->value.extension && main->value.text.empty())))
    {
        m_at.error(m_at.begin(ctx), m_at.end(ctx),
                   ext->kind + " requires " +
                       (kind->positionalAs == "ResourceKey" ? std::string("a resource key")
                                                            : "a " + std::string(kind->positionalAs)));
        return ext;
    }
    if (ext->kind == "Binding" || ext->kind == "x:Bind")
    {
        checkChoice(*ext, "Mode", {"OneTime", "OneWay", "TwoWay"});
        checkChoice(*ext, "UpdateSourceTrigger", {"Default", "PropertyChanged", "Explicit", "LostFocus"});
    }
    if (ext->kind == "RelativeSource")
        checkChoice(*ext, "Mode", {"Self", "TemplatedParent"});
    if (ext->kind == "x:Bind" && mainValue && !mainValue->extension())
    {
        // The path keeps its own source slice so x:Bind errors point inside the attribute.
        ext->bind = parseBind(m_at.text.slice(m_at.begin(mainValue), m_at.end(mainValue)), m_at.context);
    }
    return ext;
}

std::string stringLiteral(std::string_view spelling)
{
    return unescape(spelling.substr(1, spelling.size() - 2));
}

BindPath bindPath(B::PathContext* ctx, const Cursor& at)
{
    BindPath path;
    path.span = at.span(at.begin(ctx), at.end(ctx));
    for (auto* child : ctx->children)
    {
        if (auto* t = dynamic_cast<antlr4::tree::TerminalNode*>(child);
            t && t->getSymbol()->getType() == B::NAME)
        {
            auto* s = t->getSymbol();
            path.steps.push_back({BindStep::Kind::kMember, s->getText(), at.span(at.begin(s), at.end(s))});
        }
        else if (auto* index = dynamic_cast<B::IndexContext*>(child))
        {
            const std::string spelling = index->getText();
            path.steps.push_back({BindStep::Kind::kIndex,
                                  index->STRING() ? stringLiteral(spelling) : spelling,
                                  at.span(at.begin(index), at.end(index))});
        }
    }
    return path;
}

} // namespace

std::shared_ptr<const MarkupExtension> parseMarkup(const MappedText& value, MarkupContext& context)
{
    // Bound the recursion before ANTLR sees the text.
    int depth = 0, deepest = 0;
    for (char c : value.text)
    {
        if (c == '{')
            deepest = std::max(deepest, ++depth);
        else if (c == '}')
            --depth;
    }
    if (deepest > context.maxDepth + 1)
    {
        context.error(value.sourceOffset(0), value.sourceOffset(value.text.size()),
                      "markup extensions nested deeper than " + std::to_string(context.maxDepth));
        return nullptr;
    }
    Cursor at{context, value, codePointOffsets(value.text)};
    antlr4::ANTLRInputStream input(value.text);
    VeloraMarkupLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    VeloraMarkupParser parser(&tokens);
    Listener listener(context, value, at.cps, "markup extension: ");
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);
    auto* root = parser.root();
    if (listener.failed())
        return nullptr;
    return MarkupBuilder(std::move(at)).extension(root->extension(), 1, false);
}

std::optional<BindExpression> parseBind(const MappedText& text, MarkupContext& context)
{
    Cursor at{context, text, codePointOffsets(text.text)};
    antlr4::ANTLRInputStream input(text.text);
    VeloraBindLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    VeloraBindParser parser(&tokens);
    Listener listener(context, text, at.cps, "x:Bind: ");
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);
    auto* root = parser.root();
    if (listener.failed())
        return std::nullopt;
    auto* expression = root->expression();
    BindExpression out;
    out.span = at.span(at.begin(expression), at.end(expression));
    if (auto* call = expression->call())
    {
        out.call = true;
        out.path = bindPath(call->path(), at);
        for (auto* argument : call->argument())
        {
            if (auto* p = argument->path())
            {
                out.arguments.emplace_back(bindPath(p, at));
                continue;
            }
            auto* literal = argument->literal();
            BindLiteral value;
            value.span = at.span(at.begin(literal), at.end(literal));
            value.text = literal->getText();
            if (literal->INT())
                value.kind = BindLiteral::Kind::kInteger;
            else if (literal->FLOAT())
                value.kind = BindLiteral::Kind::kFloat;
            else if (literal->STRING())
            {
                value.kind = BindLiteral::Kind::kString;
                value.text = stringLiteral(value.text);
            }
            else if (literal->XNULL())
                value.kind = BindLiteral::Kind::kNull;
            else
                value.kind = BindLiteral::Kind::kBoolean;
            out.arguments.emplace_back(std::move(value));
        }
    }
    else
    {
        out.path = bindPath(expression->path(), at);
    }
    return out;
}

} // namespace velora
