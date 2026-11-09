#include <string>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/ElementSchema.hpp>

namespace velora
{

const PropertyDescriptor* schemaPropertyDescriptor(std::string_view xamlName)
{
    const auto& props = elementProperties().props;
#define VELORA_SCHEMA_DESCRIPTOR(member, name, kind, choices, value, flags)                                  \
    if (xamlName == name)                                                                                    \
        return props.member;
    VELORA_UI_PROPERTIES(VELORA_SCHEMA_DESCRIPTOR)
#undef VELORA_SCHEMA_DESCRIPTOR
    return nullptr;
}

u64 schemaFingerprint() noexcept
{
    static const u64 fingerprint = []
    {
        u64 hash = 0xCBF29CE484222325ULL;
        const auto mix = [&](std::string_view text)
        {
            for (const char c : text)
                hash = (hash ^ static_cast<u8>(c)) * 0x100000001B3ULL;
            hash = (hash ^ 0xFF) * 0x100000001B3ULL; // field separator
        };
        for (const auto& p : kSchemaProperties)
        {
            mix(p.name);
            mix(std::to_string(static_cast<int>(p.kind)));
            mix(p.choices);
            mix(p.defaultValue);
            mix(std::to_string(p.flags));
            if (const auto* d = schemaPropertyDescriptor(p.name))
                mix(std::to_string(d->id));
        }
        for (const auto& e : kSchemaEvents)
        {
            mix(e.name);
            mix(e.signature);
        }
        for (const auto& t : kSchemaTypes)
        {
            mix(t.name);
            mix(std::to_string(static_cast<int>(t.content)));
            mix(t.contentProperty);
            for (const auto property : t.properties)
                mix(property);
            for (const auto event : t.events)
                mix(event);
        }
        return hash;
    }();
    return fingerprint;
}

} // namespace velora
