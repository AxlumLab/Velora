#include "velora/reflect/Metagen.hpp"

#include <sstream>
#include <stdexcept>

namespace velora
{
namespace
{

using namespace velora;

Access access(RkAccess a)
{
    return a == RkAccess::kPublic      ? Access::kPublic
           : a == RkAccess::kProtected ? Access::kProtected
                                       : Access::kPrivate;
}

std::string methodSignature(const RkMethod& m)
{
    std::string out = (m.returnType.empty() ? "" : m.returnType + " ") + m.name + "(";
    for (std::size_t i = 0; i < m.parameters.size(); ++i)
        out += (i ? ", " : "") + m.parameters[i].type;
    out += ")";
    if (m.is_const)
        out += " const";
    if (m.isNoexcept)
        out += " noexcept";
    return out;
}

} // namespace

MetadataDocument generate(const RkAst& ast)
{
    MetadataDocument doc;
    for (const auto& type : ast.types)
    {
        TypeMetadata t;
        t.qualifiedName = type.qualifiedName();
        t.stableId = stableId("type:" + t.qualifiedName);
        t.kind = type.kind == RkType::Kind::kClass  ? TypeMetadata::Kind::kClass
                 : type.kind == RkType::Kind::kEnum ? TypeMetadata::Kind::kEnum
                                                    : TypeMetadata::Kind::kStruct;
        t.sourceLine = type.line;
        t.noSpawn = type.noSpawn;
        t.bases = type.baseTypes;
        const auto symbol = [&](const std::string& name) { return t.qualifiedName + "::" + name; };
        for (const auto& f : type.fields)
        {
            FieldMetadata m{0,        f.name,           symbol(f.name), f.type,     f.getter,
                            f.setter, access(f.access), f.isStatic,     f.readonly, f.transient,
                            f.line};
            m.stableId = stableId(t.qualifiedName + ":field:" + f.type + ":" + f.name);
            t.fields.push_back(std::move(m));
        }
        for (const auto& f : type.methods)
        {
            MethodMetadata m{0,
                             f.name,
                             symbol(f.name),
                             methodSignature(f),
                             access(f.access),
                             f.isStatic,
                             !f.isConstructor         ? ConstructorKind::kNone
                             : f.isReflectConstructor ? ConstructorKind::kReflected
                                                      : ConstructorKind::kImplicit,
                             f.line};
            m.stableId = stableId(t.qualifiedName + ":method:" + m.signature + (f.isStatic ? ":static" : ""));
            t.methods.push_back(std::move(m));
        }
        for (const auto& p : type.properties)
        {
            PropertyMetadata m{0,        p.name,         symbol(p.name), p.type, p.getter,
                               p.setter, p.defaultValue, p.flags,        p.line};
            m.stableId = stableId(t.qualifiedName + ":property:" + p.name);
            t.properties.push_back(std::move(m));
        }
        for (const auto& e : type.events)
        {
            EventMetadata m{0, e.name, symbol(e.name), e.signature, e.adder, e.line};
            m.stableId = stableId(t.qualifiedName + ":event:" + e.name);
            t.events.push_back(std::move(m));
        }
        for (const auto& v : type.enumValues)
            t.enumValues.push_back({v.name, v.value, v.raw});
        doc.types.push_back(std::move(t));
    }
    return doc;
}

Generated generateFromSource(std::string_view source, std::string_view sourceName,
                             const RkParseOptions& options)
{
    Generated out;
    const auto parsed = parseReflection(source, options);
    out.dependencies = parsed.dependencies;
    if (!parsed.ok())
    {
        for (const auto& d : parsed.diagnostics)
        {
            if (d.severity != RkDiagnostic::Severity::kError)
                continue;
            out.result.error = std::string(sourceName) + ":" + std::to_string(d.line) + ":" +
                               std::to_string(d.column) + ": " + d.message;
            break;
        }
        return out;
    }
    try
    {
        out.result.document = generate(parsed.ast);
    }
    catch (const std::exception& error)
    {
        out.result.error = std::string(sourceName) + ": " + error.what();
        return out;
    }
    out.result.text = serializeMetadata(out.result.document);
    out.result.valid = true;
    return out;
}

namespace
{

void writeMemberChecks(std::ostream& out, const MetadataDocument& document)
{
    for (const auto& t : document.types)
    {
        if (t.kind == TypeMetadata::Kind::kEnum)
            continue;
        const std::string type = "::" + t.qualifiedName;
        const auto check = [&](const std::string& expression, const std::string& what)
        {
            out << "static_assert(requires { " << expression << "; }, \"" << t.qualifiedName << ": " << what
                << "\");\n";
        };
        for (const auto& f : t.fields)
            if (f.access == Access::kPublic)
                check("&" + type + "::" + f.name, "public field " + f.name + " not found");
        for (const auto& p : t.properties)
        {
            check("std::declval<const " + type + "&>()." + p.getter + "()",
                  "property " + p.name + " getter " + p.getter + "() is not callable on a const object");
            if (!p.setter.empty())
                check("std::declval<" + type + "&>()." + p.setter +
                          "(std::declval<std::remove_cvref_t<decltype(std::declval<const " + type + "&>()." +
                          p.getter + "())>>())",
                      "property " + p.name + " setter " + p.setter +
                          "() does not accept the getter's value type");
        }
        for (const auto& e : t.events)
            check("std::declval<" + type + "&>()." + e.adder + "(std::function<" + e.signature + ">{})",
                  "event " + e.name + " adder " + e.adder + "() does not accept std::function<" +
                      e.signature + ">");
    }
}

// Runtime accessor tables (velora/reflect/Accessors.hpp) for {Binding}. Templates are skipped.
void writeAccessors(std::ostream& out, const MetadataDocument& document)
{
    out << "\nnamespace {\n";
    std::size_t n = 0;
    for (const auto& t : document.types)
    {
        if (t.kind == TypeMetadata::Kind::kEnum || t.qualifiedName.find('<') != std::string::npos)
            continue;
        const std::string type = "::" + t.qualifiedName, self = "static_cast<" + type + "*>(o)";
        std::vector<std::string> entries;
        for (const auto& p : t.properties)
        {
            std::string entry = "    velora::property(\"" + p.name +
                                "\", [](void* o) -> decltype(auto) { return " + self + "->" + p.getter +
                                "(); }";
            if (!p.setter.empty())
                entry += ",\n        [](void* o, auto&& v) { " + self + "->" + p.setter +
                         "(std::forward<decltype(v)>(v)); }";
            entries.push_back(entry + "),\n");
        }
        for (const auto& f : t.fields)
        {
            if (f.access != Access::kPublic || f.isStatic)
                continue;
            std::string entry = "    velora::property(\"" + f.name +
                                "\", [](void* o) -> decltype(auto) { return (" + self + "->" + f.name +
                                "); }";
            if (!f.readonly && !f.type.starts_with("const "))
                entry += ",\n        [](void* o, auto&& v) { " + self + "->" + f.name +
                         " = std::forward<decltype(v)>(v); }";
            entries.push_back(entry + "),\n");
        }
        const std::string table = "velora_accessors_" + std::to_string(n),
                          info = "velora_type_" + std::to_string(n);
        if (!entries.empty())
        {
            out << "const velora::PropertyAccessor " << table << "[] = {\n";
            for (const auto& e : entries)
                out << e;
            out << "};\n";
        }
        out << "const velora::TypeAccessors " << info << "{&typeid(" << type << "), \"" << t.qualifiedName
            << "\", " << (entries.empty() ? "{}" : table) << ", &velora::observableOf<" << type << ">};\n"
            << "[[maybe_unused]] const bool " << info << "_registered = velora::registerAccessors(" << info
            << ");\n";
        ++n;
    }
    out << "}  // namespace\n";
}

} // namespace

std::string generateReflectCpp(const MetadataDocument& document, std::string_view unit,
                               std::string_view include)
{
    const std::string text = serializeMetadata(document);
    std::ostringstream out;
    out << "// Generated by velorac reflect; do not edit.\n"
        << "#include <velora/reflect/Metadata.hpp>\n";
    if (!include.empty())
    {
        out << "#include <velora/reflect/Accessors.hpp>\n\n#include <functional>\n#include "
               "<type_traits>\n#include <typeinfo>\n#include <utility>\n"
            << "#include \"" << include << "\"\n\n";
        writeMemberChecks(out, document);
        writeAccessors(out, document);
    }
    out << "\nstatic_assert(velora::kMetadataSchemaVersion == " << document.schemaVersion
        << ", \"velorac output does not match the runtime metadata schema; regenerate\");\n\n"
        << "namespace {\n"
        << "const velora::GeneratedUnit velora_rkmeta_unit{\"" << unit << "\", " << document.schemaVersion
        << ",\n"
        << "    R\"rkmeta(" << text << ")rkmeta\"};\n"
        << "[[maybe_unused]] const bool velora_rkmeta_registered = "
           "velora::registerUnit(velora_rkmeta_unit);\n"
        << "}  // namespace\n";
    return out.str();
}

} // namespace velora
