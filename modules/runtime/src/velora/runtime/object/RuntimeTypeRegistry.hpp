#pragma once

#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <velora/types/TypeId.hpp>

// Runtime type metadata shared by XAML validation and the controls module. Construction uses
// ordinary C++ factories in the controls module; the registry owns names and inheritance only.
namespace std
{
template <> struct hash<velora::TypeId>
{
    size_t operator()(const velora::TypeId& id) const noexcept
    {
        return static_cast<size_t>(id.value.high ^ (id.value.low * 0x9E3779B97F4A7C15ULL));
    }
};
} // namespace std

namespace velora
{

enum class RegistrationResult : u8
{
    kOk,
    kInvalidEntry,
    kIdMismatch,
    kAlreadyRegistered,
    kUnknownBase,
    kInheritanceCycle
};

[[nodiscard]] const char* registrationResultName(RegistrationResult result) noexcept;

class RuntimeTypeRegistry final
{
public:
    RuntimeTypeRegistry() = default;
    RuntimeTypeRegistry(const RuntimeTypeRegistry&) = delete;
    RuntimeTypeRegistry& operator=(const RuntimeTypeRegistry&) = delete;

    struct Entry
    {
        TypeId typeId;
        std::string className;
        TypeId baseTypeId;
    };

    // Validates the whole batch before publishing it. A base can already be registered or occur
    // anywhere in the same batch. Invalid registrations leave the registry unchanged.
    RegistrationResult registerTypes(std::span<const Entry> entries);

    [[nodiscard]] const Entry* find(TypeId typeId) const;
    [[nodiscard]] const Entry* find(std::string_view className) const;
    [[nodiscard]] bool isA(TypeId typeId, TypeId baseTypeId) const;
    [[nodiscard]] bool isA(std::string_view className, std::string_view baseClassName) const;
    [[nodiscard]] usize size() const;

private:
    RegistrationResult validate(std::span<const Entry> entries) const;

    mutable std::shared_mutex m_mutex;
    std::unordered_map<TypeId, Entry> m_byId;
    std::unordered_map<std::string, TypeId> m_byName;
};

[[nodiscard]] std::string formatTypeId(TypeId typeId);

} // namespace velora
