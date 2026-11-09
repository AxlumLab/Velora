#include "velora/reflect/Compiler.hpp"

#include <algorithm>
#include <sstream>

namespace velora
{
namespace
{

const char* kindName(RkType::Kind kind)
{
    switch (kind)
    {
    case RkType::Kind::kStruct:
        return "struct";
    case RkType::Kind::kClass:
        return "class";
    case RkType::Kind::kEnum:
        return "enum";
    }
    return "?";
}

const char* accessName(RkAccess access)
{
    switch (access)
    {
    case RkAccess::kPublic:
        return "public";
    case RkAccess::kProtected:
        return "protected";
    case RkAccess::kPrivate:
        return "private";
    }
    return "?";
}

void writeList(std::ostream& out, const char* label, const std::vector<std::string>& items)
{
    if (items.empty())
        return;
    out << ' ' << label << '=';
    for (std::size_t i = 0; i < items.size(); ++i)
        out << (i ? "|" : "") << items[i];
}

} // namespace

bool RkParseResult::ok() const noexcept
{
    return std::ranges::none_of(diagnostics, [](const RkDiagnostic& d)
                                { return d.severity == RkDiagnostic::Severity::kError; });
}

std::string normalizeReflection(const RkAst& ast)
{
    std::ostringstream out;
    for (const auto& type : ast.types)
    {
        out << kindName(type.kind) << ' ' << type.qualifiedName() << " @" << type.line;
        if (type.noSpawn)
            out << " no_spawn";
        writeList(out, "attrs", type.attributes);
        out << '\n';
        for (const auto& base : type.baseTypes)
            out << "  base " << base << '\n';
        if (type.kind == RkType::Kind::kEnum)
        {
            out << "  underlying '" << type.underlying_type << "' class=" << type.enumClass << '\n';
            for (const auto& value : type.enumValues)
            {
                out << "  value " << value.name << " = ";
                if (value.value)
                    out << *value.value;
                else
                    out << '?';
                out << " raw='" << value.raw << "'\n";
            }
        }
        for (const auto& field : type.fields)
        {
            out << "  field " << accessName(field.access) << ' ' << (field.isStatic ? "static " : "") << '\''
                << field.type << "' " << field.name << " get=" << field.getter << " set=" << field.setter
                << " readonly=" << field.readonly << " transient=" << field.transient << " @" << field.line;
            writeList(out, "attrs", field.attributes);
            out << '\n';
        }
        for (const auto& method : type.methods)
        {
            out << "  "
                << (method.isConstructor ? (method.isReflectConstructor ? "rkctor " : "ctor ") : "method ")
                << accessName(method.access) << ' ' << (method.isStatic ? "static " : "") << '\''
                << method.returnType << "' " << method.name << '(';
            for (std::size_t i = 0; i < method.parameters.size(); ++i)
                out << (i ? ", " : "") << '\'' << method.parameters[i].type << "' "
                    << method.parameters[i].name;
            out << ')' << (method.is_const ? " const" : "") << (method.isNoexcept ? " noexcept" : "") << " @"
                << method.line;
            writeList(out, "attrs", method.attributes);
            out << '\n';
        }
        for (const auto& p : type.properties)
        {
            out << "  property " << p.name << " '" << p.type << "' get=" << p.getter << " set=" << p.setter
                << " default='" << p.defaultValue << "' @" << p.line;
            writeList(out, "flags", p.flags);
            out << '\n';
        }
        for (const auto& e : type.events)
            out << "  event " << e.name << " '" << e.signature << "' add=" << e.adder << " @" << e.line
                << '\n';
    }
    return out.str();
}

} // namespace velora
