#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <velora/globals.hpp>

// Reflection declaration frontend: the ANTLR4-generated parser for the restricted declaration
// subset (modules/markup/grammar/reflect), producing a frontend-neutral Ast. It does not run a
// C preprocessor. Directives are evaluated against an explicit macro
// context (RkParseOptions::defines plus #define lines seen in the file and its includes).
namespace velora
{

enum class RkAccess
{
    kPublic,
    kProtected,
    kPrivate
};

struct RkParameter
{
    std::string type, name;
};

struct RkField
{
    std::string type, name, getter, setter;
    std::vector<std::string> attributes;
    bool readonly = false, transient = false, isStatic = false;
    RkAccess access = RkAccess::kPublic;
    std::size_t line = 0;
};

struct RkMethod
{
    std::string returnType, name;
    std::vector<RkParameter> parameters;
    std::vector<std::string> attributes;
    bool isStatic = false, is_const = false, isNoexcept = false;
    bool isConstructor = false, isReflectConstructor = false;
    RkAccess access = RkAccess::kPublic;
    std::size_t line = 0;
};

// rkproperty(Name, Type, get=fn, set=fn, default=value, Flag...): a UI property backed by
// accessor functions. Flags: Measure, Arrange, Paint, Inherited, Content, Attached.
struct RkProperty
{
    std::string name, type, getter, setter, defaultValue;
    std::vector<std::string> flags;
    std::size_t line = 0;
};

// rkevent(Name, Signature, add=fn): an event whose handler is installed by `add`.
struct RkEvent
{
    std::string name, signature, adder;
    std::size_t line = 0;
};

struct RkEnumValue
{
    std::string name;
    std::optional<i64> value; // unset when the initializer is not an integer literal
    std::string raw;          // initializer spelling, empty when implicit
};

struct RkType
{
    enum class Kind
    {
        kStruct,
        kClass,
        kEnum
    } kind = Kind::kStruct;
    std::string name;
    std::string scope; // enclosing namespaces and types joined with "::"
    std::vector<std::string> baseTypes;
    std::vector<std::string> attributes;
    std::vector<RkField> fields;
    std::vector<RkMethod> methods;
    std::vector<RkProperty> properties; // ANTLR frontend only
    std::vector<RkEvent> events;        // ANTLR frontend only
    std::vector<RkEnumValue> enumValues;
    std::string underlying_type;
    bool enumClass = false;
    bool noSpawn = false; // R_OBJECT_NO_SPAWN
    std::size_t line = 0;

    std::string qualifiedName() const
    {
        return scope.empty() ? name : scope + "::" + name;
    }
};

// Reflected types in declaration order; a nested type follows its enclosing type.
struct RkAst
{
    std::vector<RkType> types;
};

struct RkDiagnostic
{
    enum class Severity
    {
        kError,
        kWarning
    };
    std::size_t line = 0, column = 0; // 1-based
    std::string message;
    Severity severity = Severity::kError;
};

struct RkParseResult
{
    RkAst ast;
    std::vector<RkDiagnostic> diagnostics;
    std::vector<std::filesystem::path> dependencies; // files read besides the source (includes)
    bool ok() const noexcept;
};

struct RkParseOptions
{
    std::vector<std::string> defines;  // build macro context, e.g. platform and export macros
    std::filesystem::path includeRoot; // #include "x" is scanned for #define lines; empty disables
};

RkParseResult parseReflection(std::string_view source, const RkParseOptions& options = {});

// Canonical text used for golden files.
std::string normalizeReflection(const RkAst& ast);

} // namespace velora
