#include <stdexcept>
#include <velora/types/TypeId.hpp>
#include <velora/runtime/property/PropertyTypes.hpp>

namespace velora
{

u64 propertyIdFor(std::string_view owner, std::string_view name) noexcept
{
    // A separate prefix distinguishes properties from type identities; the
    // '|' separator keep property names out of those spaces and keep ("a.b", "c") apart from
    // ("a", "b.c").
    std::string key;
    key.reserve(16 + owner.size() + 1 + name.size());
    key.append("Velora.Property.").append(owner).append("|").append(name);
    const Uuid uuid = uuidV5(kVeloraUuidNamespace, key);
    // 0 never names a property.
    const u64 id = uuid.high ^ uuid.low;
    return id ? id : 1;
}

const PropertyDescriptor& PropertyRegistry::define(PropertyDescriptor metadata)
{
    std::string key = metadata.owner.empty() ? metadata.name : metadata.owner + "." + metadata.name;
    if (metadata.name.empty())
        throw std::invalid_argument("a property needs a name");
    if (m_values.contains(key))
        throw std::runtime_error("duplicate property: " + key);
    metadata.id = propertyIdFor(metadata.owner, metadata.name);
    if (const auto clash = m_ids.find(metadata.id); clash != m_ids.end())
        throw std::runtime_error("property id collision: " + key + " and " + clash->second);
    m_ids.emplace(metadata.id, key);
    return m_values.emplace(std::move(key), std::move(metadata)).first->second;
}

ReadOnlyPropertyKey PropertyRegistry::defineReadOnly(PropertyDescriptor metadata)
{
    metadata.readOnly = true;
    return ReadOnlyPropertyKey(define(std::move(metadata)));
}

const PropertyDescriptor& PropertyRegistry::define(std::string name, PropertyValue defaultValue,
                                                   PropertyFlags flags, bool inherited)
{
    PropertyDescriptor metadata;
    metadata.name = std::move(name);
    metadata.defaultValue = std::move(defaultValue);
    metadata.flags = flags;
    metadata.inherited = inherited;
    return define(std::move(metadata));
}

const PropertyDescriptor& PropertyRegistry::define(std::string owner, std::string name,
                                                   PropertyValue defaultValue, PropertyFlags flags,
                                                   bool inherited, PropertyKind kind)
{
    // Owner-scoped properties are always keyed "owner.name", even with an empty owner, as before.
    PropertyDescriptor metadata;
    metadata.owner = std::move(owner);
    metadata.name = std::move(name);
    metadata.defaultValue = std::move(defaultValue);
    metadata.flags = flags;
    metadata.inherited = inherited;
    metadata.kind = kind;
    std::string key = metadata.owner + "." + metadata.name;
    if (m_values.contains(key))
        throw std::runtime_error("duplicate property: " + key);
    metadata.id = propertyIdFor(metadata.owner, metadata.name);
    if (const auto clash = m_ids.find(metadata.id); clash != m_ids.end())
        throw std::runtime_error("property id collision: " + key + " and " + clash->second);
    m_ids.emplace(metadata.id, key);
    return m_values.emplace(std::move(key), std::move(metadata)).first->second;
}

const PropertyDescriptor* PropertyRegistry::find(std::string_view name) const noexcept
{
    const auto it = m_values.find(name);
    return it == m_values.end() ? nullptr : &it->second;
}

const PropertyDescriptor* PropertyRegistry::find(std::string_view owner, std::string_view name) const
{
    return find(std::string(owner) + "." + std::string(name));
}

} // namespace velora
