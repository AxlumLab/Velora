#include "velora/reflect/Metadata.hpp"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <velora/globals.hpp>

namespace velora
{
namespace
{

const char* toString(Access access)
{
    return access == Access::kPublic ? "public" : access == Access::kProtected ? "protected" : "private";
}
const char* toString(TypeMetadata::Kind kind)
{
    return kind == TypeMetadata::Kind::kClass  ? "class"
           : kind == TypeMetadata::Kind::kEnum ? "enum"
                                               : "struct";
}
const char* toString(ConstructorKind kind)
{
    return kind == ConstructorKind::kImplicit    ? "implicit"
           : kind == ConstructorKind::kReflected ? "reflected"
                                                 : "none";
}

std::string join(const std::vector<std::string>& items, char separator)
{
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i)
            out += separator;
        out += items[i];
    }
    return out;
}

std::vector<std::string> split(std::string_view text, char separator)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true)
    {
        const auto end = text.find(separator, start);
        out.emplace_back(
            text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos)
            return out;
        start = end + 1;
    }
}

template <class T> bool number(const std::string& text, T& out)
{
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}

bool flag(const std::string& text, bool& out)
{
    if (text != "0" && text != "1")
        return false;
    out = text == "1";
    return true;
}

template <class E>
bool parseEnum(const std::string& text, std::initializer_list<std::pair<const char*, E>> values, E& out)
{
    for (const auto& [name, value] : values)
        if (text == name)
        {
            out = value;
            return true;
        }
    return false;
}

bool parseAccess(const std::string& text, Access& out)
{
    return parseEnum(
        text, {{"public", Access::kPublic}, {"protected", Access::kProtected}, {"private", Access::kPrivate}},
        out);
}

// Strips cv-qualifiers, references and pointers: "const std::string&" -> "std::string".
std::string_view bareType(std::string_view type)
{
    while (type.starts_with("const "))
        type.remove_prefix(6);
    while (!type.empty() && (type.back() == '&' || type.back() == '*' || type.back() == ' '))
        type.remove_suffix(1);
    if (type.ends_with(" const"))
        type.remove_suffix(6);
    return type;
}

} // namespace

bool PropertyMetadata::hasFlag(std::string_view value) const
{
    return std::ranges::find(flags, value) != flags.end();
}

bool TypeMetadata::constructible() const
{
    bool declared = false;
    for (const auto& m : methods)
    {
        if (m.constructor == ConstructorKind::kNone)
            continue;
        declared = true;
        if (m.access == Access::kPublic)
            return true;
    }
    return !declared;
}

u64 stableId(std::string_view key)
{
    u64 hash = 1469598103934665603ull; // FNV-1a 64
    for (unsigned char c : key)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash ? hash : 1;
}

std::string serializeMetadata(const MetadataDocument& document)
{
    std::ostringstream out;
    out << "rkmeta\t" << document.schemaVersion << '\n';
    for (const auto& t : document.types)
    {
        out << "type\t" << t.stableId << '\t' << t.qualifiedName << '\t' << toString(t.kind) << '\t'
            << t.sourceLine << '\t' << t.noSpawn << '\n';
        for (const auto& b : t.bases)
            out << "base\t" << b << '\n';
        for (const auto& f : t.fields)
            out << "field\t" << f.stableId << '\t' << f.name << '\t' << f.symbol << '\t' << toString(f.access)
                << '\t' << f.isStatic << '\t' << f.sourceLine << '\t' << f.type << '\t' << f.getter << '\t'
                << f.setter << '\t' << f.readonly << '\t' << f.transient << '\n';
        for (const auto& m : t.methods)
            out << "method\t" << m.stableId << '\t' << m.name << '\t' << m.symbol << '\t'
                << toString(m.access) << '\t' << m.isStatic << '\t' << toString(m.constructor) << '\t'
                << m.sourceLine << '\t' << m.signature << '\n';
        for (const auto& p : t.properties)
            out << "property\t" << p.stableId << '\t' << p.name << '\t' << p.symbol << '\t' << p.sourceLine
                << '\t' << p.type << '\t' << p.getter << '\t' << p.setter << '\t' << p.defaultValue << '\t'
                << join(p.flags, '|') << '\n';
        for (const auto& e : t.events)
            out << "event\t" << e.stableId << '\t' << e.name << '\t' << e.symbol << '\t' << e.sourceLine
                << '\t' << e.signature << '\t' << e.adder << '\n';
        for (const auto& v : t.enumValues)
            out << "value\t" << v.name << '\t' << (v.value ? std::to_string(*v.value) : std::string()) << '\t'
                << v.raw << '\n';
    }
    return out.str();
}

MetadataResult parseMetadata(std::string_view text)
{
    MetadataResult result;
    std::size_t lineNo = 0;
    const auto fail = [&](std::string message)
    {
        result = {};
        result.error = "rkmeta line " + std::to_string(lineNo) + ": " + message;
        return result;
    };
    std::istringstream input{std::string(text)};
    std::string line;
    ++lineNo;
    if (!std::getline(input, line))
        return fail("empty document");
    const auto header = split(line, '\t');
    int version = 0;
    if (header.size() != 2 || header[0] != "rkmeta" || !number(header[1], version))
        return fail("missing rkmeta header");
    if (version != kMetadataSchemaVersion && version != 3 && version != 4)
        return fail("schema " + std::to_string(version) + " does not match runtime schema " +
                    std::to_string(kMetadataSchemaVersion));

    MetadataDocument& doc = result.document;
    doc.schemaVersion = version;
    std::unordered_set<u64> ids;
    const auto unique = [&](u64 id) { return ids.insert(id).second; };
    while (std::getline(input, line))
    {
        ++lineNo;
        const auto c = split(line, '\t');
        const std::string& kind = c[0];
        if (kind != "type" && doc.types.empty())
            return fail("'" + kind + "' row before any type");
        TypeMetadata* t = doc.types.empty() ? nullptr : &doc.types.back();
        if (kind == "type")
        {
            TypeMetadata type;
            if (c.size() != 6 || !number(c[1], type.stableId) || !number(c[4], type.sourceLine) ||
                !flag(c[5], type.noSpawn) ||
                !parseEnum(c[3],
                           {{"struct", TypeMetadata::Kind::kStruct},
                            {"class", TypeMetadata::Kind::kClass},
                            {"enum", TypeMetadata::Kind::kEnum}},
                           type.kind))
                return fail("malformed type row");
            type.qualifiedName = c[2];
            if (type.stableId != stableId("type:" + type.qualifiedName))
                return fail("type id does not match " + type.qualifiedName);
            if (!unique(type.stableId))
                return fail("duplicate type " + type.qualifiedName);
            doc.types.push_back(std::move(type));
        }
        else if (kind == "base")
        {
            if (c.size() != 2 || c[1].empty())
                return fail("malformed base row");
            t->bases.push_back(c[1]);
        }
        else if (kind == "field")
        {
            FieldMetadata f;
            if (c.size() != 12 || !number(c[1], f.stableId) || !parseAccess(c[4], f.access) ||
                !flag(c[5], f.isStatic) || !number(c[6], f.sourceLine) || !flag(c[10], f.readonly) ||
                !flag(c[11], f.transient))
                return fail("malformed field row");
            f.name = c[2];
            f.symbol = c[3];
            f.type = c[7];
            f.getter = c[8];
            f.setter = c[9];
            if (!unique(f.stableId))
                return fail("duplicate member id for " + f.symbol);
            t->fields.push_back(std::move(f));
        }
        else if (kind == "method")
        {
            MethodMetadata m;
            if (c.size() != 9 || !number(c[1], m.stableId) || !parseAccess(c[4], m.access) ||
                !flag(c[5], m.isStatic) ||
                !parseEnum(c[6],
                           {{"none", ConstructorKind::kNone},
                            {"implicit", ConstructorKind::kImplicit},
                            {"reflected", ConstructorKind::kReflected}},
                           m.constructor) ||
                !number(c[7], m.sourceLine))
                return fail("malformed method row");
            m.name = c[2];
            m.symbol = c[3];
            m.signature = c[8];
            if (!unique(m.stableId))
                return fail("duplicate member id for " + m.symbol);
            t->methods.push_back(std::move(m));
        }
        else if (kind == "property")
        {
            PropertyMetadata p;
            if (c.size() != 10 || !number(c[1], p.stableId) || !number(c[4], p.sourceLine))
                return fail("malformed property row");
            p.name = c[2];
            p.symbol = c[3];
            p.type = c[5];
            p.getter = c[6];
            p.setter = c[7];
            p.defaultValue = c[8];
            if (!c[9].empty())
                p.flags = split(c[9], '|');
            if (!unique(p.stableId))
                return fail("duplicate member id for " + p.symbol);
            t->properties.push_back(std::move(p));
        }
        else if (kind == "event")
        {
            EventMetadata e;
            if (c.size() != 7 || !number(c[1], e.stableId) || !number(c[4], e.sourceLine))
                return fail("malformed event row");
            e.name = c[2];
            e.symbol = c[3];
            e.signature = c[5];
            e.adder = c[6];
            if (!unique(e.stableId))
                return fail("duplicate member id for " + e.symbol);
            t->events.push_back(std::move(e));
        }
        else if (kind == "value")
        {
            EnumValueMetadata v;
            i64 value = 0;
            if (c.size() != 4 || t->kind != TypeMetadata::Kind::kEnum ||
                (!c[2].empty() && !number(c[2], value)))
                return fail("malformed enum value row");
            v.name = c[1];
            if (!c[2].empty())
                v.value = value;
            v.raw = c[3];
            t->enumValues.push_back(std::move(v));
        }
        else
        {
            return fail("unknown row '" + kind + "'");
        }
    }
    result.valid = true;
    result.text = std::string(text);
    return result;
}

void MetadataCatalog::add(const MetadataDocument& document)
{
    m_types.insert(m_types.end(), document.types.begin(), document.types.end());
}

const TypeMetadata* MetadataCatalog::find(std::string_view name, std::string_view context) const
{
    name = bareType(name);
    if (name.starts_with("::"))
        name.remove_prefix(2);
    const auto exact = [&](std::string_view qualified) -> const TypeMetadata*
    {
        for (const auto& t : m_types)
            if (t.qualifiedName == qualified)
                return &t;
        return nullptr;
    };
    // Relative to the enclosing scopes of the context type, innermost first.
    for (std::string scope(context);;)
    {
        if (const auto* t = exact(scope.empty() ? std::string(name) : scope + "::" + std::string(name)))
            return t;
        if (scope.empty())
            break;
        const auto cut = scope.rfind("::");
        scope = cut == std::string::npos ? std::string() : scope.substr(0, cut);
    }
    const TypeMetadata* found = nullptr;
    for (const auto& t : m_types)
    {
        const std::string_view q = t.qualifiedName;
        if (q == name ||
            (q.size() > name.size() && q.ends_with(name) && q.substr(q.size() - name.size() - 2, 2) == "::"))
        {
            if (found)
                return nullptr; // ambiguous short name
            found = &t;
        }
    }
    return found;
}

MetadataCatalog::Member MetadataCatalog::member(const TypeMetadata& type, std::string_view name,
                                                int depth) const
{
    for (const auto& p : type.properties)
        if (p.name == name)
            return {nullptr, &p, &type};
    for (const auto& f : type.fields)
        if (f.name == name)
            return {&f, nullptr, &type};
    if (depth < 16)
    {
        for (const auto& base : type.bases)
        {
            const std::string_view baseName =
                base.starts_with("virtual ") ? std::string_view(base).substr(8) : std::string_view(base);
            if (const auto* b = find(baseName, type.qualifiedName))
                if (auto m = member(*b, name, depth + 1); m.owner)
                    return m;
        }
    }
    return {};
}

BindingResult MetadataCatalog::resolve(std::string_view rootType, std::string_view path,
                                       bool requireWritable) const
{
    BindingResult result;
    const TypeMetadata* current = find(rootType);
    if (!current)
    {
        result.error = "unknown type '" + std::string(rootType) + "'";
        return result;
    }
    const auto segments = split(path, '.');
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        const auto& name = segments[i];
        if (!current)
        {
            result.error = "'" + result.type + "' has no reflection metadata; cannot bind '" + name + "'";
            return result;
        }
        const auto m = member(*current, name);
        if (!m.owner)
        {
            result.error = "'" + current->qualifiedName + "' has no bindable member '" + name + "'";
            return result;
        }
        BindingStep step{name, {}, m.property != nullptr, false};
        if (m.property)
        {
            step.type = m.property->type;
            step.writable = !m.property->setter.empty();
            step.getter = m.property->getter;
            step.setter = m.property->setter;
        }
        else
        {
            if (m.field->access != Access::kPublic)
            {
                result.error = "'" + m.field->symbol + "' is not public";
                return result;
            }
            step.type = m.field->type;
            step.writable = !m.field->readonly && !m.field->type.starts_with("const ") && !m.field->isStatic;
        }
        result.type = step.type;
        result.writable = step.writable;
        result.steps.push_back(step);
        current = find(step.type, m.owner->qualifiedName);
    }
    if (requireWritable && !result.writable)
    {
        result.error = "'" + std::string(path) + "' is read-only; TwoWay binding needs a setter";
        return result;
    }
    result.ok = true;
    return result;
}

namespace
{
std::vector<const GeneratedUnit*>& units()
{
    static std::vector<const GeneratedUnit*> list;
    return list;
}
std::mutex& unitsMutex()
{
    static std::mutex m;
    return m;
}
} // namespace

bool registerUnit(const GeneratedUnit& unit)
{
    if (unit.schemaVersion != kMetadataSchemaVersion)
        return false;
    std::scoped_lock lock(unitsMutex());
    units().push_back(&unit);
    return true;
}

std::span<const GeneratedUnit* const> generatedUnits()
{
    std::scoped_lock lock(unitsMutex());
    return units();
}

} // namespace velora
