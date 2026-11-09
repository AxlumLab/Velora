#include <algorithm>
#include <sstream>
#include <utility>
#include <variant>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/runtime/SchemaValue.hpp>

namespace velora
{
u64 XamlPropertyDeclaration::id() const
{
    return propertyIdFor({}, runtimeName);
}

const XamlPropertyDeclaration* XamlTypeDeclaration::property(std::string_view name) const
{
    for (const auto& p : properties)
        if (p.name == name)
            return &p;
    return nullptr;
}

u64 XamlTypeDeclaration::contract() const
{
    u64 hash = 14695981039346656037ULL;
    const auto mix = [&](std::string_view value)
    {
        for (const unsigned char c : value)
            hash = (hash ^ c) * 1099511628211ULL;
        hash = (hash ^ 0xFF) * 1099511628211ULL;
    };
    mix(className);
    mix(baseType);
    std::vector<const XamlPropertyDeclaration*> sorted;
    for (const auto& p : properties)
        sorted.push_back(&p);
    std::ranges::sort(sorted, {}, [](const auto* p) { return p->name; });
    for (const auto* p : sorted)
    {
        mix(p->name);
        mix(p->runtimeName);
        mix(std::to_string(static_cast<u32>(p->kind)));
        mix(p->defaultValue);
        mix("Paint");
    }
    return hash ? hash : 1;
}

const XamlTypeDeclaration* XamlTypeCatalog::find(std::string_view className) const
{
    for (const auto& type : m_types)
        if (type.className == className)
            return &type;
    return nullptr;
}

std::string XamlTypeCatalog::append(std::string_view text)
{
    std::istringstream input{std::string(text)};
    std::string line;
    u32 number = 0;
    std::vector<XamlTypeDeclaration> pending;
    XamlTypeDeclaration* current = nullptr;
    const auto error = [&](std::string message) { return std::to_string(number) + ": " + message; };
    const auto identifier = [](std::string_view name, bool qualified)
    {
        bool start = true;
        for (const unsigned char c : name)
        {
            if (qualified && c == '.' && !start)
            {
                start = true;
                continue;
            }
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
                  (!start && c >= '0' && c <= '9')))
                return false;
            start = false;
        }
        return !start;
    };
    while (std::getline(input, line))
    {
        ++number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (number == 1)
        {
            if (line != "velora-xaml-types\t1")
                return error("unsupported type declaration header");
            continue;
        }
        if (line.empty() || line.starts_with('#'))
            continue;
        std::vector<std::string> fields;
        std::istringstream row(line);
        for (std::string field; std::getline(row, field, '\t');)
            fields.push_back(std::move(field));
        if (line.ends_with('\t'))
            fields.emplace_back();
        if (fields[0] == "type" && fields.size() == 3 && !current)
        {
            if (!identifier(fields[1], true) || fields[1].find('.') == std::string::npos ||
                fields[1].starts_with("Velora.UI.") || !findSchemaType(fields[2]))
                return error("invalid source type or unknown built-in base");
            if (find(fields[1]) ||
                std::ranges::any_of(pending, [&](const auto& t) { return t.className == fields[1]; }))
                return error("duplicate type '" + fields[1] + "'");
            pending.push_back({fields[1], fields[2], {}});
            current = &pending.back();
        }
        else if (fields[0] == "property" && fields.size() == 5 && current)
        {
            XamlPropertyDeclaration p{fields[1], ValueKind::kDouble, fields[3], fields[4]};
            if (!identifier(p.name, false) || !identifier(p.runtimeName, true) || current->property(p.name) ||
                hasSchemaProperty(*findSchemaType(current->baseType), p.name))
                return error("invalid, duplicate or inherited property '" + p.name + "'");
            if (fields[2] == "Double")
                p.kind = ValueKind::kDouble;
            else if (fields[2] == "Integer")
                p.kind = ValueKind::kInteger;
            else if (fields[2] == "Boolean")
                p.kind = ValueKind::kBoolean;
            else if (fields[2] == "String")
            {
                p.kind = ValueKind::kString;
                if (p.defaultValue == "\"\"")
                    p.defaultValue.clear();
            }
            else
                return error("unsupported property kind");
            const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
            if (std::holds_alternative<std::string>(parseValue(schema, p.defaultValue)))
                return error("invalid property default");
            for (const auto& t : pending)
                for (const auto& other : t.properties)
                    if (other.id() == p.id())
                        return error("duplicate runtime property identity");
            for (const auto& t : m_types)
                for (const auto& other : t.properties)
                    if (other.id() == p.id())
                        return error("duplicate runtime property identity");
            current->properties.push_back(std::move(p));
        }
        else if (fields.size() == 1 && fields[0] == "end" && current)
            current = nullptr;
        else
            return error("unexpected declaration row");
    }
    if (!number || current || pending.empty())
        return error("incomplete type declaration");
    auto combined = m_types;
    combined.insert(combined.end(), pending.begin(), pending.end());
    m_types.swap(combined);
    return {};
}
} // namespace velora
