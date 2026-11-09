#include <array>
#include <cstdio>
#include <string>
#include <velora/runtime/object/BuiltinTypes.hpp>
#include <velora/runtime/object/RuntimeTypeRegistry.hpp>

using namespace velora;

namespace
{
int failures = 0;

void check(bool condition, const char* description)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", description);
        ++failures;
    }
}

RuntimeTypeRegistry::Entry entry(const char* name, const char* base = nullptr)
{
    return {typeIdFor(name), name, base ? typeIdFor(base) : TypeId{}};
}

void registration()
{
    RuntimeTypeRegistry types;
    check(registerBuiltinTypes(types) == RegistrationResult::kOk, "base types register");
    check(types.isA(kControlClassName, kDependencyObjectClassName), "base hierarchy is preserved");
    check(types.find(kControlClassName) == types.find(typeIdFor(kControlClassName)),
          "name and id refer to the same registered type");

    const std::array batch{entry("Example.Child", "Example.Base"),
                           entry("Example.Base", kControlClassName)};
    check(types.registerTypes(batch) == RegistrationResult::kOk,
          "a batch accepts base types declared after derived types");
    check(types.isA("Example.Child", kDependencyObjectClassName), "inheritance crosses batches");
    check(!types.isA(kControlClassName, "Example.Child"), "inheritance is directional");
    check(types.isA("Example.Child", "Example.Child"), "a type is assignable to itself");
    check(types.find("Missing.Type") == nullptr, "unknown types are not fabricated");

    const usize size = types.size();
    check(types.registerTypes(batch) == RegistrationResult::kAlreadyRegistered,
          "duplicate registrations are rejected");
    check(types.size() == size, "duplicates do not change the registry");

    const std::array unknownBase{entry("Example.Valid", kControlClassName),
                                 entry("Example.Invalid", "Missing.Base")};
    check(types.registerTypes(unknownBase) == RegistrationResult::kUnknownBase,
          "an unknown base rejects the whole batch");
    check(types.size() == size && !types.find("Example.Valid"), "failed batches do not partly register");

    const std::array cycle{entry("Example.CycleA", "Example.CycleB"),
                           entry("Example.CycleB", "Example.CycleA")};
    check(types.registerTypes(cycle) == RegistrationResult::kInheritanceCycle,
          "cyclic inheritance is rejected");
    check(types.size() == size, "a cycle leaves existing types unchanged");

    auto mismatch = entry("Example.Renamed", kControlClassName);
    mismatch.typeId = typeIdFor("Example.Original");
    const std::array wrongIdentity{mismatch};
    check(types.registerTypes(wrongIdentity) == RegistrationResult::kIdMismatch,
          "a name cannot reuse an unrelated type id");
    check(types.size() == size, "invalid identities leave the registry unchanged");
}

void identity()
{
    const auto first = typeIdFor("Velora.UI.Button");
    check(first == typeIdFor("Velora.UI.Button"), "type ids are deterministic");
    check(first != typeIdFor("Velora.UI.TextBox"), "different names have different type ids");
    check(formatTypeId(first).size() == 36, "type ids have a diagnostic UUID representation");
}
}

int main()
{
    registration();
    identity();
    std::printf("type registry: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
