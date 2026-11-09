#include <velora/globals.hpp>
// ANTLR4-generated reflection frontend. Grammar: modules/markup/grammar/reflect/VeloraReflect*.g4.
#include "VeloraReflectLexer.h"
#include "VeloraReflectParser.h"
#include "velora/reflect/Compiler.hpp"

#include <antlr4-runtime.h>
#include <cctype>
#include <charconv>
#include <fstream>
#include <set>
#include <sstream>

namespace velora
{
namespace
{

using P = VeloraReflectParser;
using Tokens = std::vector<antlr4::Token*>;

// ---------------------------------------------------------------------------
// Preprocessing in an explicit macro context. Directive and inactive lines are
// blanked so token lines stay identical to the source.

struct MacroEvent
{
    std::size_t line;
    std::string name;
    enum class Kind
    {
        kContextDefine,
        kContextUndefine,
        kFileDefine,
        kFileUndefine
    } kind;
};

class MacroTable
{
public:
    explicit MacroTable(const std::vector<std::string>& defines)
    {
        for (const auto& d : defines)
            m_events.push_back({0, d, MacroEvent::Kind::kContextDefine});
    }
    void add(MacroEvent event)
    {
        m_events.push_back(std::move(event));
    }
    // Whether `name` is known as a macro for code on `line`, matching the legacy parser:
    // context macros (options and includes) and file #defines are tracked separately.
    bool defined(std::string_view name, std::size_t line) const
    {
        bool context = false, file = false;
        for (const auto& e : m_events)
        {
            if (e.line >= line)
                break;
            if (e.name != name)
                continue;
            switch (e.kind)
            {
            case MacroEvent::Kind::kContextDefine:
                context = true;
                break;
            case MacroEvent::Kind::kContextUndefine:
                context = false;
                break;
            case MacroEvent::Kind::kFileDefine:
                file = true;
                break;
            case MacroEvent::Kind::kFileUndefine:
                file = false;
                break;
            }
        }
        return context || file;
    }

private:
    std::vector<MacroEvent> m_events;
};

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::string_view takeIdentifier(std::string_view& s)
{
    s = trim(s);
    std::size_t n = 0;
    while (n < s.size() && (std::isalnum(static_cast<unsigned char>(s[n])) || s[n] == '_'))
        ++n;
    const auto id = s.substr(0, n);
    s.remove_prefix(n);
    return id;
}

// Updates block-comment state across a line; returns whether the line begins outside a comment.
bool scanComments(std::string_view line, bool& inBlock)
{
    const bool startsOutside = !inBlock;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        const char next = i + 1 < line.size() ? line[i + 1] : '\0';
        if (inBlock)
        {
            if (c == '*' && next == '/')
            {
                inBlock = false;
                ++i;
            }
        }
        else if (quote)
        {
            if (c == '\\')
                ++i;
            else if (c == quote)
                quote = 0;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
        }
        else if (c == '/' && next == '/')
        {
            break;
        }
        else if (c == '/' && next == '*')
        {
            inBlock = true;
            ++i;
        }
    }
    return startsOutside;
}

struct Preprocessed
{
    std::string text;
    std::vector<RkDiagnostic> diagnostics;
    std::vector<std::filesystem::path> dependencies;
};

Preprocessed preprocess(std::string_view source, const RkParseOptions& options, MacroTable& macros)
{
    struct Frame
    {
        bool parentActive, active, taken;
        std::size_t line;
    };
    std::vector<Frame> frames;
    Preprocessed out;
    bool inBlock = false;
    std::size_t lineNo = 0;
    std::size_t pos = 0;
    const auto active = [&] { return frames.empty() || frames.back().active; };
    const auto known = [&](std::string_view name) { return macros.defined(name, lineNo + 1); };
    // Legacy #if: only `defined(X)`, a known macro name, or the literal 1 are true.
    const auto evaluate = [&](std::string_view expr)
    {
        std::string_view rest = trim(expr);
        const auto word = takeIdentifier(rest);
        if (word == "defined")
        {
            rest = trim(rest);
            if (rest.empty() || rest.front() != '(')
                return false;
            rest.remove_prefix(1);
            return known(takeIdentifier(rest));
        }
        if (!word.empty())
            return !std::isdigit(static_cast<unsigned char>(word.front())) ? known(word) : word == "1";
        return false;
    };

    while (pos <= source.size())
    {
        std::size_t end = source.find('\n', pos);
        if (end == std::string_view::npos)
            end = source.size();
        std::string_view line = source.substr(pos, end - pos);
        ++lineNo;
        const std::size_t firstLine = lineNo;
        const bool outsideComment = scanComments(line, inBlock);
        std::string_view body = trim(line);
        const bool directive = outsideComment && !body.empty() && body.front() == '#';
        std::string logical(line);
        std::size_t linesUsed = 1;
        if (directive)
        {
            // Join backslash continuations; every physical line is blanked.
            while (!logical.empty() && trim(logical).ends_with('\\') && end < source.size())
            {
                const std::size_t nextEnd = std::min(source.find('\n', end + 1), source.size());
                logical = std::string(trim(logical));
                logical.pop_back();
                logical += ' ';
                logical += source.substr(end + 1, nextEnd - end - 1);
                end = nextEnd;
                ++linesUsed;
            }
            std::string_view rest = trim(logical);
            rest.remove_prefix(1);
            const auto word = takeIdentifier(rest);
            const bool wasActive = active();
            if (word == "if" || word == "ifdef" || word == "ifndef")
            {
                bool cond = false;
                if (wasActive)
                {
                    if (word == "if")
                        cond = evaluate(rest);
                    else
                    {
                        const bool isKnown = known(takeIdentifier(rest));
                        cond = word == "ifdef" ? isKnown : !isKnown;
                    }
                }
                frames.push_back({wasActive, wasActive && cond, cond, firstLine});
            }
            else if (word == "elif" || word == "else")
            {
                if (frames.empty())
                {
                    out.diagnostics.push_back({firstLine, 1, "#" + std::string(word) + " without #if"});
                }
                else
                {
                    auto& f = frames.back();
                    const bool cond = word == "else" || (f.parentActive && !f.taken && evaluate(rest));
                    f.active = f.parentActive && !f.taken && cond;
                    f.taken = f.taken || f.active;
                }
            }
            else if (word == "endif")
            {
                if (frames.empty())
                    out.diagnostics.push_back({firstLine, 1, "#endif without #if"});
                else
                    frames.pop_back();
            }
            else if (wasActive && word == "define")
            {
                macros.add({firstLine, std::string(takeIdentifier(rest)), MacroEvent::Kind::kFileDefine});
            }
            else if (wasActive && word == "undef")
            {
                macros.add({firstLine, std::string(takeIdentifier(rest)), MacroEvent::Kind::kFileUndefine});
            }
            else if (wasActive && word == "include" && !options.includeRoot.empty())
            {
                rest = trim(rest);
                if (rest.size() > 2 && (rest.front() == '"' || rest.front() == '<'))
                {
                    const char close = rest.front() == '"' ? '"' : '>';
                    const auto stop = rest.find(close, 1);
                    const auto path = options.includeRoot / std::string(rest.substr(1, stop - 1));
                    std::ifstream included(path);
                    if (included)
                        out.dependencies.push_back(path);
                    // Same rule as the legacy include scan: '#define X' / '#undef X' lines only.
                    for (std::string text; std::getline(included, text);)
                    {
                        std::istringstream row(text);
                        std::string head, id;
                        row >> head >> id;
                        if (head == "#define")
                            macros.add({firstLine, id, MacroEvent::Kind::kContextDefine});
                        else if (head == "#undef")
                            macros.add({firstLine, id, MacroEvent::Kind::kContextUndefine});
                    }
                }
            }
            for (std::size_t i = 0; i < linesUsed; ++i)
                out.text += '\n';
            lineNo += linesUsed - 1;
        }
        else
        {
            if (active())
                out.text += logical;
            out.text += '\n';
        }
        pos = end + 1;
    }
    out.text.pop_back();
    for (const auto& f : frames)
        out.diagnostics.push_back({f.line, 1, "unterminated conditional directive"});
    return out;
}

// ---------------------------------------------------------------------------

class CollectingErrors final : public antlr4::BaseErrorListener
{
public:
    explicit CollectingErrors(std::vector<RkDiagnostic>& out) : m_out(out) {}
    void syntaxError(antlr4::Recognizer*, antlr4::Token*, size_t line, size_t column,
                     const std::string& message, std::exception_ptr) override
    {
        // ANTLR lists every viable token; long sets carry no useful information.
        std::string text = message;
        if (const auto at = text.find(" expecting {");
            at != std::string::npos && std::ranges::count(text, ',') > 4)
            text.erase(at);
        m_out.push_back({line, column + 1, std::move(text)});
    }

private:
    std::vector<RkDiagnostic>& m_out;
};

void flatten(antlr4::tree::ParseTree* tree, Tokens& out)
{
    if (auto* terminal = dynamic_cast<antlr4::tree::TerminalNode*>(tree))
    {
        if (terminal->getSymbol()->getType() != antlr4::Token::EOF)
            out.push_back(terminal->getSymbol());
        return;
    }
    for (auto* child : tree->children)
        flatten(child, out);
}

Tokens tokensOf(antlr4::tree::ParseTree* tree)
{
    Tokens out;
    if (tree)
        flatten(tree, out);
    return out;
}

bool is(const antlr4::Token* t, std::size_t type)
{
    return t && t->getType() == type;
}
bool isText(const antlr4::Token* t, std::string_view text)
{
    return t && t->getType() == P::ID && t->getText() == text;
}

// Canonical spelling shared with the legacy frontend: tight around '::', before ',' ';' ')' ']' '>',
// after '(' '[' '<', before '(' '[' '<', and on both sides of '&' '&&' '|' '*'.
bool joinsTightly(std::size_t prev, std::size_t next)
{
    const auto any = [](std::size_t t, std::initializer_list<std::size_t> set)
    { return std::ranges::find(set, t) != set.end(); };
    return prev == P::SCOPE || next == P::SCOPE ||
           any(next, {P::COMMA, P::SEMI, P::RPAREN, P::RBRACKET, P::GT}) ||
           any(prev, {P::LPAREN, P::LBRACKET, P::LT}) || any(next, {P::LPAREN, P::LBRACKET, P::LT}) ||
           any(next, {P::AMP, P::ANDAND, P::PIPE, P::STAR}) ||
           any(prev, {P::AMP, P::ANDAND, P::PIPE, P::STAR});
}

std::string spell(const Tokens& tokens, std::size_t begin = 0, std::size_t end = std::string::npos)
{
    end = std::min(end, tokens.size());
    std::string out;
    for (std::size_t i = begin; i < end; ++i)
    {
        if (i > begin && !joinsTightly(tokens[i - 1]->getType(), tokens[i]->getType()))
            out += ' ';
        out += tokens[i]->getText();
    }
    return std::string(trim(out));
}

// Splits at depth-0 commas, tracking (), [], {} and <>.
std::vector<Tokens> splitCommas(const Tokens& tokens)
{
    std::vector<Tokens> items(1);
    int depth = 0;
    for (auto* t : tokens)
    {
        const auto type = t->getType();
        if (type == P::LPAREN || type == P::LBRACKET || type == P::LBRACE || type == P::LT)
            ++depth;
        else if ((type == P::RPAREN || type == P::RBRACKET || type == P::RBRACE || type == P::GT) &&
                 depth > 0)
            --depth;
        if (type == P::COMMA && depth == 0)
            items.emplace_back();
        else
            items.back().push_back(t);
    }
    return items;
}

Tokens inner(const Tokens& group)
{
    return group.size() < 2 ? Tokens{} : Tokens(group.begin() + 1, group.end() - 1);
}

std::optional<i64> integerLiteral(std::string_view text)
{
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        text.remove_prefix(2);
    }
    std::size_t n = 0;
    while (n < text.size() && std::isxdigit(static_cast<unsigned char>(text[n])))
        ++n;
    for (char c : text.substr(n))
        if (c != 'u' && c != 'U' && c != 'l' && c != 'L')
            return std::nullopt;
    i64 value = 0;
    const auto r = std::from_chars(text.data(), text.data() + n, value, base);
    if (r.ec != std::errc{} || r.ptr != text.data() + n)
        return std::nullopt;
    return value;
}

bool isTypeKeyword(std::string_view s)
{
    static constexpr std::string_view kWords[] = {
        "void", "bool",   "char",     "char8_t", "char16_t", "char32_t", "wchar_t", "short",   "int",
        "long", "signed", "unsigned", "float",   "double",   "auto",     "const",   "volatile"};
    return std::ranges::find(kWords, s) != std::end(kWords);
}

bool isQualifier(std::string_view s)
{
    return s == "const" || s == "volatile" || s == "typename" || s == "struct" || s == "class" || s == "enum";
}

class Builder
{
public:
    Builder(RkParseResult& result, const MacroTable& macros) : m_result(result), m_macros(macros) {}

    void translationUnit(P::TranslationUnitContext* ctx)
    {
        for (auto* d : ctx->declaration())
            declaration(d, {});
    }

private:
    void error(const antlr4::Token* at, std::string message)
    {
        m_result.diagnostics.push_back(
            {at ? at->getLine() : 1, at ? at->getCharPositionInLine() + 1 : 1, std::move(message)});
    }
    void warning(const antlr4::Token* at, std::string message)
    {
        m_result.diagnostics.push_back({at->getLine(), at->getCharPositionInLine() + 1, std::move(message),
                                        RkDiagnostic::Severity::kWarning});
    }
    std::vector<std::string> takeAttributes()
    {
        return std::exchange(m_pending, {});
    }

    void declaration(P::DeclarationContext* ctx, const std::string& scope)
    {
        if (auto* ns = ctx->namespaceDecl())
        {
            std::string name;
            if (auto* q = ns->qualifiedName())
                for (auto* id : q->ID())
                    name += (name.empty() ? "" : "::") + id->getText();
            const std::string innerScope = scope.empty() ? name : name.empty() ? scope : scope + "::" + name;
            for (auto* d : ns->declaration())
                declaration(d, innerScope);
        }
        else if (auto* t = ctx->templateDecl())
        {
            templateDecl(t);
        }
        else if (auto* a = ctx->attributeDecl())
        {
            attribute(a);
        }
        else if (auto* r = ctx->reflectedType())
        {
            reflected(r, scope);
        }
    }

    void templateDecl(P::TemplateDeclContext* ctx)
    {
        if (ctx->reflectedType())
            warning(ctx->reflectedType()->getStart(), "template declarations are not reflected");
        else if (auto* nested = ctx->templateDecl())
            templateDecl(nested);
    }

    void attribute(P::AttributeDeclContext* ctx)
    {
        for (const auto& item : splitCommas(inner(tokensOf(ctx->parenGroup()))))
            if (auto text = spell(item); !text.empty())
                m_pending.push_back(std::move(text));
    }

    void reflected(P::ReflectedTypeContext* ctx, const std::string& scope)
    {
        if (auto* c = ctx->classDecl())
            classDecl(c, scope);
        else
            enumDecl(ctx->enumDecl(), scope);
    }

    void classDecl(P::ClassDeclContext* ctx, const std::string& scope)
    {
        const antlr4::Token* name = nullptr;
        for (auto* child : ctx->classHead()->children)
            if (auto* t = dynamic_cast<antlr4::tree::TerminalNode*>(child);
                t && t->getText() != "final" && t->getText() != "sealed")
                name = t->getSymbol();
        if (!name)
            return error(ctx->getStart(), "expected declaration name");

        const std::size_t index = m_result.ast.types.size();
        RkType& type = m_result.ast.types.emplace_back();
        type.kind = ctx->RKCLASS() ? RkType::Kind::kClass : RkType::Kind::kStruct;
        type.name = name->getText();
        type.scope = scope;
        type.line = ctx->getStart()->getLine();
        type.attributes = takeAttributes();
        for (auto* base : ctx->baseSpecifier())
        {
            Tokens kept;
            for (auto* t : tokensOf(base))
                if (!is(t, P::PUBLIC) && !is(t, P::PROTECTED) && !is(t, P::PRIVATE))
                    kept.push_back(t);
            if (!kept.empty())
                type.baseTypes.push_back(spell(kept));
        }
        const std::string memberScope = type.qualifiedName();
        const RkAccess outer = std::exchange(m_access, type.kind == RkType::Kind::kClass ? RkAccess::kPrivate
                                                                                         : RkAccess::kPublic);
        for (auto* m : ctx->member())
            member(m, index, memberScope);
        m_access = outer;
    }

    RkType& typeAt(std::size_t index)
    {
        return m_result.ast.types[index];
    }

    void member(P::MemberContext* ctx, std::size_t index, const std::string& scope)
    {
        if (auto* label = ctx->accessLabel())
        {
            m_access = label->PUBLIC()      ? RkAccess::kPublic
                       : label->PROTECTED() ? RkAccess::kProtected
                                            : RkAccess::kPrivate;
        }
        else if (auto* p = ctx->propertyDecl())
        {
            propertyDecl(p, index);
        }
        else if (auto* e = ctx->eventDecl())
        {
            eventDecl(e, index);
        }
        else if (auto* a = ctx->attributeDecl())
            attribute(a);
        else if (auto* r = ctx->reflectedType())
            reflected(r, scope);
        else if (auto* f = ctx->fieldDecl())
            fieldDecl(f, index);
        else if (auto* f = ctx->functionDecl())
            functionDecl(f, index);
        else if (auto* c = ctx->constructorDecl())
            constructorDecl(c, index);
        else if (auto* m = ctx->macroInvocation())
        {
            if (typeAt(index).kind == RkType::Kind::kClass &&
                m->ID()->getText().find("NO_SPAWN") != std::string::npos)
                typeAt(index).noSpawn = true;
        }
        else if (auto* d = ctx->memberDecl())
            memberDecl(d, index);
    }

    // Applies one cfield(...) argument with the RkGen positional rules.
    static void applyMeta(RkField& field, const std::string& item, int positional)
    {
        if (item.empty())
            return;
        if (const auto eq = item.find('='); eq != std::string::npos)
        {
            const std::string key(trim(std::string_view(item).substr(0, eq)));
            const std::string value(trim(std::string_view(item).substr(eq + 1)));
            if (key == "get")
                field.getter = value;
            else if (key == "set")
                field.setter = value;
            else
                field.attributes.push_back(item);
        }
        else if (item == "readonly")
            field.readonly = true;
        else if (item == "transient")
            field.transient = true;
        else if (item.starts_with("attr"))
            field.attributes.push_back(item);
        else if (positional == 0)
            field.getter = item;
        else if (positional == 1)
            field.setter = item;
        else
            field.attributes.push_back(item);
    }

    void fieldDecl(P::FieldDeclContext* ctx, std::size_t index)
    {
        RkField field;
        field.line = ctx->RKFIELD()->getSymbol()->getLine();
        field.isStatic = !ctx->STATIC().empty();
        if (auto* meta = ctx->fieldMeta())
        {
            int positional = 0;
            for (auto* child : meta->children)
            {
                if (auto* t = dynamic_cast<antlr4::tree::TerminalNode*>(child);
                    t && is(t->getSymbol(), P::COMMA))
                    ++positional;
                else if (auto* item = dynamic_cast<P::MetaItemContext*>(child))
                    applyMeta(field, spell(tokensOf(item)), positional);
            }
        }
        if (ctx->declarator().size() > 1)
            return error(ctx->COMMA(0)->getSymbol(), "one declarator per rkfield");
        Tokens decl;
        for (auto* t : tokensOf(ctx->declarator(0)))
        {
            if (is(t, P::ASSIGN) || is(t, P::LBRACE))
                break;
            if (is(t, P::STATIC))
                field.isStatic = true;
            else
                decl.push_back(t);
        }
        for (auto* t : decl)
        {
            if (is(t, P::LBRACKET))
                return error(t, "array members are not supported by reflection");
            if (is(t, P::COLON))
                return error(t, "bit-field members cannot be reflected");
        }
        if (decl.size() < 2 || !is(decl.back(), P::ID))
            return error(ctx->RKFIELD()->getSymbol(), "invalid rkfield declaration, need [type] [name]");
        field.name = decl.back()->getText();
        field.type = spell(decl, 0, decl.size() - 1);
        for (auto& a : takeAttributes())
            field.attributes.push_back(std::move(a));
        field.access = m_access;
        typeAt(index).fields.push_back(std::move(field));
    }

    std::vector<RkParameter> parameters(const Tokens& list)
    {
        std::vector<RkParameter> out;
        if (list.empty())
            return out;
        int index = 0;
        for (Tokens param : splitCommas(list))
        {
            if (const auto eq = std::ranges::find_if(param, [](auto* t) { return is(t, P::ASSIGN); });
                eq != param.end())
                param.erase(eq, param.end());
            if (param.empty() || (param.size() == 1 && isText(param[0], "void")))
                continue;
            const auto* last = param.back();
            bool named = param.size() > 1 && is(last, P::ID) && !isTypeKeyword(last->getText()) &&
                         !is(param[param.size() - 2], P::SCOPE);
            if (named)
                named = std::ranges::any_of(param.begin(), param.end() - 1,
                                            [](auto* t) { return !isQualifier(t->getText()); });
            if (named)
                out.push_back({spell(param, 0, param.size() - 1), last->getText()});
            else
                out.push_back({spell(param), "_arg" + std::to_string(index)});
            ++index;
        }
        return out;
    }

    static void readTail(const Tokens& tail, RkMethod& method)
    {
        for (auto* t : tail)
        {
            if (isText(t, "const"))
                method.is_const = true;
            if (isText(t, "noexcept"))
                method.isNoexcept = true;
        }
    }

    void functionDecl(P::FunctionDeclContext* ctx, std::size_t index)
    {
        RkMethod method;
        const auto* marker = ctx->RKFUNC()->getSymbol();
        method.line = marker->getLine();
        method.isStatic = !ctx->STATIC().empty();
        if (auto* group = ctx->parenGroup())
            for (auto* t : inner(tokensOf(group)))
                method.isStatic = method.isStatic || is(t, P::STATIC);
        Tokens sig;
        for (auto* t : tokensOf(ctx->signature()))
        {
            if (is(t, P::STATIC))
            {
                method.isStatic = true;
                continue;
            }
            if (is(t, P::INLINE) || isText(t, "constexpr") || isText(t, "virtual"))
                continue;
            if (is(t, P::ID) && m_macros.defined(t->getText(), t->getLine()))
                continue;
            sig.push_back(t);
        }
        if (sig.size() < 2 || !is(sig.back(), P::ID))
            return error(marker, "invalid function declaration, need [type] [name]");
        method.name = sig.back()->getText();
        method.returnType = spell(sig, 0, sig.size() - 1);
        method.parameters = parameters(inner(tokensOf(ctx->parameters())));
        readTail(tokensOf(ctx->functionTail()), method);
        method.attributes = takeAttributes();
        method.access = m_access;
        typeAt(index).methods.push_back(std::move(method));
    }

    void constructorDecl(P::ConstructorDeclContext* ctx, std::size_t index)
    {
        const auto* name = ctx->ID().empty() ? nullptr : ctx->ID().back()->getSymbol();
        if (!name || name->getText() != typeAt(index).name)
            return error(name ? name : ctx->getStart(), "expected constructor name");
        addConstructor(name, inner(tokensOf(ctx->parameters())), index, true);
    }

    void addConstructor(const antlr4::Token* name, const Tokens& params, std::size_t index, bool reflect)
    {
        RkMethod method;
        method.name = name->getText();
        method.line = name->getLine();
        method.isConstructor = true;
        method.isReflectConstructor = reflect;
        method.parameters = parameters(params);
        method.attributes = takeAttributes();
        method.access = m_access;
        typeAt(index).methods.push_back(std::move(method));
    }

    void memberDecl(P::MemberDeclContext* ctx, std::size_t index)
    {
        Tokens decl;
        bool hasBody = false;
        for (auto* child : ctx->children)
        {
            if (dynamic_cast<P::BraceGroupContext*>(child))
                hasBody = true;
            else
                flatten(child, decl);
        }
        if (!decl.empty() && is(decl.back(), P::SEMI))
            decl.pop_back();
        bool isStatic = false;
        std::size_t first = 0;
        while (first < decl.size() && is(decl[first], P::STATIC))
        {
            isStatic = true;
            ++first;
        }
        if (first == decl.size())
            return;
        RkType& type = typeAt(index);

        // Implicit constructor: Name( ... ) or explicit Name( ... ).
        std::size_t name = first + (isText(decl[first], "explicit") ? 1 : 0);
        if (name + 1 < decl.size() && isText(decl[name], type.name) && is(decl[name + 1], P::LPAREN))
        {
            int depth = 0;
            std::size_t close = name + 1;
            for (; close < decl.size(); ++close)
            {
                if (is(decl[close], P::LPAREN))
                    ++depth;
                else if (is(decl[close], P::RPAREN) && --depth == 0)
                    break;
            }
            return addConstructor(decl[name], Tokens(decl.begin() + name + 2, decl.begin() + close), index,
                                  false);
        }
        if (type.kind == RkType::Kind::kClass)
        {
            if (is(decl[first], P::ID) && decl[first]->getText().find("NO_SPAWN") != std::string::npos)
                type.noSpawn = true;
            return;
        }

        // Struct members without markers are reflected as implicit fields.
        static constexpr std::string_view kSkipped[] = {"using",     "typedef", "friend", "template",
                                                        "constexpr", "inline",  "extern"};
        if (std::ranges::find(kSkipped, decl[first]->getText()) != std::end(kSkipped))
            return;
        // A nested unreflected type definition, not a member.
        static constexpr std::string_view kTypeKeys[] = {"struct", "class", "union", "enum"};
        if (hasBody && std::ranges::find(kTypeKeys, decl[first]->getText()) != std::end(kTypeKeys))
            return;
        const auto attributes = takeAttributes();
        Tokens field;
        int angle = 0;
        for (std::size_t i = first; i < decl.size(); ++i)
        {
            const auto* t = decl[i];
            if (is(t, P::LT))
                ++angle;
            else if (is(t, P::GT) && angle > 0)
                --angle;
            if (angle == 0 && is(t, P::ASSIGN))
                break;
            if (angle == 0 && (is(t, P::LPAREN) || isText(t, "operator") || t->getText() == "~"))
                return; // function
            field.push_back(decl[i]);
        }
        if (std::ranges::any_of(field, [](auto* t) { return is(t, P::LBRACKET) || is(t, P::COLON); }))
        {
            return warning(field.front(), "array and bit-field members are not reflected");
        }
        if (field.size() < 2 || !is(field.back(), P::ID))
            return;
        type.fields.push_back({spell(field, 0, field.size() - 1),
                               field.back()->getText(),
                               {},
                               {},
                               attributes,
                               false,
                               false,
                               isStatic,
                               m_access,
                               field.front()->getLine()});
    }

    // Splits rkproperty/rkevent arguments into positional items and key=value pairs.
    static std::vector<std::string> metaItems(antlr4::ParserRuleContext* ctx)
    {
        std::vector<std::string> items;
        for (auto* child : ctx->children)
            if (auto* item = dynamic_cast<P::MetaItemContext*>(child))
                items.push_back(spell(tokensOf(item)));
        return items;
    }

    static std::optional<std::pair<std::string, std::string>> keyValue(const std::string& item)
    {
        const auto eq = item.find('=');
        if (eq == std::string::npos)
            return std::nullopt;
        return std::pair{std::string(trim(std::string_view(item).substr(0, eq))),
                         std::string(trim(std::string_view(item).substr(eq + 1)))};
    }

    void propertyDecl(P::PropertyDeclContext* ctx, std::size_t index)
    {
        static constexpr std::string_view kKnownFlags[] = {"Measure",   "Arrange", "Paint",
                                                           "Inherited", "Content", "Attached"};
        const auto* at = ctx->RKPROPERTY()->getSymbol();
        const auto items = metaItems(ctx);
        if (items.size() < 2 || keyValue(items[0]) || keyValue(items[1]))
            return error(at, "rkproperty needs a name and a type: rkproperty(Name, Type, ...)");
        RkProperty property{items[0], items[1], {}, {}, {}, {}, at->getLine()};
        for (std::size_t i = 2; i < items.size(); ++i)
        {
            if (const auto kv = keyValue(items[i]))
            {
                if (kv->first == "get")
                    property.getter = kv->second;
                else if (kv->first == "set")
                    property.setter = kv->second;
                else if (kv->first == "default")
                    property.defaultValue = kv->second;
                else
                    return error(at, "unknown rkproperty key '" + kv->first + "'");
            }
            else if (std::ranges::find(kKnownFlags, items[i]) != std::end(kKnownFlags))
            {
                property.flags.push_back(items[i]);
            }
            else
            {
                return error(at, "unknown rkproperty flag '" + items[i] + "'");
            }
        }
        if (property.getter.empty())
            return error(at, "rkproperty '" + property.name + "' needs get=");
        typeAt(index).properties.push_back(std::move(property));
    }

    void eventDecl(P::EventDeclContext* ctx, std::size_t index)
    {
        const auto* at = ctx->RKEVENT()->getSymbol();
        const auto items = metaItems(ctx);
        if (items.size() < 2 || keyValue(items[0]) || keyValue(items[1]))
            return error(at, "rkevent needs a name and a signature: rkevent(Name, void(Args), add=fn)");
        RkEvent event{items[0], items[1], {}, at->getLine()};
        for (std::size_t i = 2; i < items.size(); ++i)
        {
            const auto kv = keyValue(items[i]);
            if (!kv || kv->first != "add")
                return error(at, "rkevent accepts only add=fn after the signature");
            event.adder = kv->second;
        }
        if (event.adder.empty())
            return error(at, "rkevent '" + event.name + "' needs add=");
        typeAt(index).events.push_back(std::move(event));
    }

    void enumDecl(P::EnumDeclContext* ctx, const std::string& scope)
    {
        RkType& type = m_result.ast.types.emplace_back();
        type.kind = RkType::Kind::kEnum;
        type.name = ctx->ID()->getText();
        type.scope = scope;
        type.line = ctx->getStart()->getLine();
        type.enumClass = ctx->CLASS() || ctx->STRUCT();
        type.attributes = takeAttributes();
        if (auto* base = ctx->enumBase())
            type.underlying_type = spell(tokensOf(base));
        std::optional<i64> next = 0;
        if (auto* list = ctx->enumerators())
        {
            for (auto* e : list->enumerator())
            {
                RkEnumValue value{e->ID()->getText(), next, {}};
                if (auto* expr = e->expression())
                {
                    const Tokens tokens = tokensOf(expr);
                    value.raw = spell(tokens);
                    value.value = std::nullopt;
                    if (tokens.size() == 1 && is(tokens[0], P::NUMBER))
                        value.value = integerLiteral(tokens[0]->getText());
                    else if (tokens.size() == 2 && tokens[0]->getText() == "-" && is(tokens[1], P::NUMBER))
                        if (auto v = integerLiteral(tokens[1]->getText()))
                            value.value = -*v;
                }
                next = value.value ? std::optional(*value.value + 1) : std::nullopt;
                type.enumValues.push_back(std::move(value));
            }
        }
    }

    RkParseResult& m_result;
    const MacroTable& m_macros;
    std::vector<std::string> m_pending;
    RkAccess m_access = RkAccess::kPublic;
};

} // namespace

RkParseResult parseReflection(std::string_view source, const RkParseOptions& options)
{
    RkParseResult result;
    MacroTable macros(options.defines);
    auto pre = preprocess(source, options, macros);
    result.diagnostics = std::move(pre.diagnostics);
    result.dependencies = std::move(pre.dependencies);

    antlr4::ANTLRInputStream input(pre.text);
    VeloraReflectLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    VeloraReflectParser parser(&tokens);
    CollectingErrors errors(result.diagnostics);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errors);
    parser.removeErrorListeners();
    parser.addErrorListener(&errors);
    auto* tree = parser.translationUnit();
    if (!result.ok())
    {
        // Explain the most common unsupported construct behind a syntax error.
        bool inFunction = false;
        for (auto* t : tokens.getTokens())
        {
            if (t->getType() == P::RKFUNC)
                inFunction = true;
            else if (t->getType() == P::SEMI || t->getType() == P::LBRACE)
                inFunction = false;
            else if (inFunction && t->getText() == "operator")
            {
                result.diagnostics.push_back(
                    {t->getLine(), t->getCharPositionInLine() + 1, "reflected operators are not supported"});
                inFunction = false;
            }
        }
        return result;
    }
    Builder(result, macros).translationUnit(tree);
    if (!result.ok())
        result.ast.types.clear();
    return result;
}

} // namespace velora
