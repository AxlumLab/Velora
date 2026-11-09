#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <velora/globals.hpp>

// Runtime side of the reflection metadata: the versioned `.rkmeta` document, its parser,
// a catalog for static binding checks and the registry of generated units. Producing
// metadata from C++ headers is build-time only (velora/reflect/Metagen.hpp).
namespace velora
{

// Bump whenever the text format or the generated code contract changes. Generated
// `.reflect.cpp` files static_assert against this value.
inline constexpr int kMetadataSchemaVersion = 5;

enum class Access
{
    kPublic,
    kProtected,
    kPrivate
};
enum class ConstructorKind
{
    kNone,
    kImplicit,
    kReflected
};

struct FieldMetadata
{
    u64 stableId = 0;
    std::string name, symbol, type, getter, setter;
    Access access = Access::kPublic;
    bool isStatic = false, readonly = false, transient = false;
    std::size_t sourceLine = 0;
};

struct MethodMetadata
{
    u64 stableId = 0;
    std::string name, symbol, signature; // signature: return type, name, parameter types, qualifiers
    Access access = Access::kPublic;
    bool isStatic = false;
    ConstructorKind constructor = ConstructorKind::kNone;
    std::size_t sourceLine = 0;
};

struct PropertyMetadata
{
    u64 stableId = 0;
    std::string name, symbol, type, getter, setter, defaultValue;
    std::vector<std::string> flags; // Measure, Arrange, Paint, Inherited, Content, Attached
    std::size_t sourceLine = 0;

    bool hasFlag(std::string_view flag) const;
};

struct EventMetadata
{
    u64 stableId = 0;
    std::string name, symbol, signature, adder;
    std::size_t sourceLine = 0;
};

struct EnumValueMetadata
{
    std::string name;
    std::optional<i64> value;
    std::string raw;
};

struct TypeMetadata
{
    enum class Kind
    {
        kStruct,
        kClass,
        kEnum
    } kind = Kind::kStruct;
    u64 stableId = 0;
    std::string qualifiedName; // also the C++ symbol
    std::size_t sourceLine = 0;
    bool noSpawn = false;
    std::vector<std::string> bases;
    std::vector<FieldMetadata> fields;
    std::vector<MethodMetadata> methods;
    std::vector<PropertyMetadata> properties;
    std::vector<EventMetadata> events;
    std::vector<EnumValueMetadata> enumValues;

    bool constructible() const; // has a public constructor, or none declared
};

struct MetadataDocument
{
    int schemaVersion = kMetadataSchemaVersion;
    std::vector<TypeMetadata> types;
};

struct MetadataResult
{
    bool valid = false;
    std::string text;
    MetadataDocument document;
    std::string error;
    bool ok() const noexcept
    {
        return valid;
    }
};

u64 stableId(std::string_view key);
// Canonical text of a document (what parse() reads back byte for byte).
std::string serializeMetadata(const MetadataDocument& document);
MetadataResult parseMetadata(std::string_view text);

// Static binding checks over one or more documents.
struct BindingStep
{
    std::string member, type;
    bool isProperty = false, writable = false;
    std::string getter, setter; // accessor functions for properties; empty for fields
};
struct BindingResult
{
    bool ok = false;
    std::string error;
    std::vector<BindingStep> steps;
    std::string type;      // type of the final member
    bool writable = false; // the final member accepts writes (TwoWay)
};

class MetadataCatalog
{
public:
    void add(const MetadataDocument& document);
    // Exact qualified name, else a name relative to `context` scopes, else a unique short name.
    const TypeMetadata* find(std::string_view name, std::string_view context = {}) const;
    // Resolves a dotted path ("Selected.Name") from `rootType` through public fields and properties,
    // searching base types. `requireWritable` checks the final member for TwoWay binding.
    BindingResult resolve(std::string_view rootType, std::string_view path,
                          bool requireWritable = false) const;

private:
    struct Member
    {
        const FieldMetadata* field = nullptr;
        const PropertyMetadata* property = nullptr;
        const TypeMetadata* owner = nullptr;
    };
    Member member(const TypeMetadata& type, std::string_view name, int depth = 0) const;
    std::vector<TypeMetadata> m_types;
};

// Registry of metadata compiled into the program by generated `.reflect.cpp` files.
struct GeneratedUnit
{
    const char* name;
    int schemaVersion;
    const char* rkmeta;
};
bool registerUnit(const GeneratedUnit& unit);
std::span<const GeneratedUnit* const> generatedUnits();

} // namespace velora
