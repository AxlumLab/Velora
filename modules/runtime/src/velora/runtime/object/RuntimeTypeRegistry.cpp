#include <cstdio>
#include <mutex>
#include <velora/runtime/object/RuntimeTypeRegistry.hpp>

namespace velora
{

const char* registrationResultName(RegistrationResult result) noexcept
{
    switch (result)
    {
    case RegistrationResult::kOk:
        return "ok";
    case RegistrationResult::kInvalidEntry:
        return "invalid entry";
    case RegistrationResult::kIdMismatch:
        return "type id is not derived from the class name";
    case RegistrationResult::kAlreadyRegistered:
        return "type already registered";
    case RegistrationResult::kUnknownBase:
        return "base type is not registered";
    case RegistrationResult::kInheritanceCycle:
        return "type inheritance contains a cycle";
    }
    return "unknown";
}

std::string formatTypeId(TypeId typeId)
{
    const u64 high = typeId.value.high, low = typeId.value.low;
    char text[37];
    std::snprintf(text, sizeof(text), "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(high >> 32),
                  static_cast<unsigned>((high >> 16) & 0xFFFF), static_cast<unsigned>(high & 0xFFFF),
                  static_cast<unsigned>(low >> 48), static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));
    return text;
}

RegistrationResult RuntimeTypeRegistry::validate(std::span<const Entry> entries) const
{
    std::unordered_map<TypeId, const Entry*> staged;
    staged.reserve(entries.size());
    for (const auto& entry : entries)
    {
        if (entry.className.empty())
            return RegistrationResult::kInvalidEntry;
        if (entry.typeId.empty() || entry.typeId != typeIdFor(entry.className))
            return RegistrationResult::kIdMismatch;
        if (m_byId.contains(entry.typeId) || m_byName.contains(entry.className) ||
            !staged.emplace(entry.typeId, &entry).second)
            return RegistrationResult::kAlreadyRegistered;
    }
    for (const auto& entry : entries)
    {
        if (!entry.baseTypeId.empty() && !m_byId.contains(entry.baseTypeId) &&
            !staged.contains(entry.baseTypeId))
            return RegistrationResult::kUnknownBase;
    }
    // Previously registered types cannot point to new entries. Follow only staged links to
    // reject self-inheritance and cycles within the batch before any metadata is published.
    for (const auto& entry : entries)
    {
        TypeId current = entry.baseTypeId;
        usize depth = 0;
        while (!current.empty())
        {
            if (current == entry.typeId || ++depth > entries.size())
                return RegistrationResult::kInheritanceCycle;
            const auto next = staged.find(current);
            if (next == staged.end())
                break;
            current = next->second->baseTypeId;
        }
    }
    return RegistrationResult::kOk;
}

RegistrationResult RuntimeTypeRegistry::registerTypes(std::span<const Entry> entries)
{
    std::unique_lock lock(m_mutex);
    if (const auto result = validate(entries); result != RegistrationResult::kOk)
        return result;
    for (const auto& entry : entries)
    {
        m_byName.emplace(entry.className, entry.typeId);
        m_byId.emplace(entry.typeId, entry);
    }
    return RegistrationResult::kOk;
}

const RuntimeTypeRegistry::Entry* RuntimeTypeRegistry::find(TypeId typeId) const
{
    std::shared_lock lock(m_mutex);
    const auto it = m_byId.find(typeId);
    // Entries are never removed, so the pointer stays valid after the lock is released.
    return it == m_byId.end() ? nullptr : &it->second;
}

const RuntimeTypeRegistry::Entry* RuntimeTypeRegistry::find(std::string_view className) const
{
    std::shared_lock lock(m_mutex);
    const auto name = m_byName.find(std::string(className));
    if (name == m_byName.end())
        return nullptr;
    return &m_byId.at(name->second);
}

bool RuntimeTypeRegistry::isA(TypeId typeId, TypeId baseTypeId) const
{
    std::shared_lock lock(m_mutex);
    if (!m_byId.contains(typeId) || !m_byId.contains(baseTypeId))
        return false;
    for (usize depth = 0; depth <= m_byId.size() && !typeId.empty(); ++depth)
    {
        if (typeId == baseTypeId)
            return true;
        const auto it = m_byId.find(typeId);
        if (it == m_byId.end())
            return false;
        typeId = it->second.baseTypeId;
    }
    return false;
}

bool RuntimeTypeRegistry::isA(std::string_view className, std::string_view baseClassName) const
{
    return isA(typeIdFor(className), typeIdFor(baseClassName));
}

usize RuntimeTypeRegistry::size() const
{
    std::shared_lock lock(m_mutex);
    return m_byId.size();
}

} // namespace velora
