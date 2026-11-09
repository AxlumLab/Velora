#include <array>
#include <velora/runtime/object/BuiltinTypes.hpp>

namespace velora
{

RegistrationResult registerBuiltinTypes(RuntimeTypeRegistry& registry)
{
    const auto entry = [](const char* name, const char* base) -> RuntimeTypeRegistry::Entry
    {
        return {typeIdFor(name), name, base ? typeIdFor(base) : TypeId{}};
    };
    const std::array entries{
        entry(kObjectClassName, nullptr),
        entry(kDependencyObjectClassName, kObjectClassName),
        entry(kUIElementClassName, kDependencyObjectClassName),
        entry(kFrameworkElementClassName, kUIElementClassName),
        entry(kControlClassName, kFrameworkElementClassName),
    };
    return registry.registerTypes(entries);
}

} // namespace velora
