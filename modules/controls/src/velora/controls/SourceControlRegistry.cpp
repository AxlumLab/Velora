#include <array>
#include <utility>
#include <variant>
#include <velora/controls/ControlTypes.hpp>
#include <velora/controls/ElementSchema.hpp>
#include <velora/controls/SourceControlRegistry.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/runtime/object/BuiltinTypes.hpp>

namespace velora
{
SourceControlRegistry::SourceControlRegistry()
{
    registerBuiltinTypes(m_types);
    registerControlTypes(m_types);
}

std::string SourceControlRegistry::add(const XamlTypeDeclaration& declaration, Factory factory,
                                       std::span<const PropertyDescriptor* const> properties)
{
    const auto* base = findSchemaType(declaration.baseType);
    if (!base || declaration.className.empty() || !factory || m_types.find(declaration.className) ||
        properties.size() != declaration.properties.size())
        return "invalid or duplicate source control registration";
    Entry entry{declaration, std::move(factory), {}};
    for (const auto& p : declaration.properties)
    {
        const PropertyDescriptor* descriptor = nullptr;
        for (const auto* d : properties)
            if (d && d->id == p.id())
                descriptor = d;
        const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
        const auto value = parseValue(schema, p.defaultValue);
        if (!descriptor || descriptor->name != p.runtimeName || !descriptor->owner.empty() ||
            descriptor->readOnly || descriptor->inherited || descriptor->flags != PropertyFlags::kPaint ||
            !std::holds_alternative<PropertyValue>(value) ||
            descriptor->defaultValue != std::get<PropertyValue>(value) || hasSchemaProperty(*base, p.name) ||
            schemaPropertyDescriptor(p.runtimeName) || entry.properties.contains(p.runtimeName))
            return "property contract mismatch for '" + p.name + "'";
        entry.properties.emplace(p.runtimeName, descriptor);
        for (const auto& [name, existing] : m_entries)
            if (existing.properties.contains(p.runtimeName))
                return "duplicate source property identity";
    }
    auto pending = m_entries;
    pending.emplace(declaration.className, std::move(entry));
    const std::array types = {RuntimeTypeRegistry::Entry{typeIdFor(declaration.className),
                                                         declaration.className,
                                                         typeIdFor("Velora.UI." + declaration.baseType)}};
    const auto result = m_types.registerTypes(types);
    if (result != RegistrationResult::kOk)
        return registrationResultName(result);
    m_entries.swap(pending);
    return {};
}

const PropertyDescriptor* SourceControlRegistry::property(std::string_view qualifiedName) const
{
    for (const auto& [name, entry] : m_entries)
        if (const auto found = entry.properties.find(std::string(qualifiedName));
            found != entry.properties.end())
            return found->second;
    return nullptr;
}

bool SourceControlRegistry::ownsProperty(std::string_view className, std::string_view qualifiedName) const
{
    const auto found = m_entries.find(std::string(className));
    return found != m_entries.end() && found->second.properties.contains(std::string(qualifiedName));
}

bool SourceControlRegistry::contains(std::string_view className) const
{
    return m_entries.contains(std::string(className));
}

bool SourceControlRegistry::matches(const XamlIRType& type) const
{
    const auto found = m_entries.find(type.className);
    return found != m_entries.end() && type.id == typeIdFor(type.className) && type.contract &&
           type.contract == found->second.declaration.contract();
}

std::shared_ptr<UIElement> SourceControlRegistry::create(const XamlIRType& type, std::string& error) const
{
    const auto found = m_entries.find(type.className);
    if (!matches(type))
    {
        error = "missing source type or type contract mismatch for '" + type.className + "'";
        return {};
    }
    // A factory may register another source type, which publishes a new entry map. Keep the
    // current declaration and callable alive independently throughout that callback.
    const auto entry = found->second;
    auto object = entry.factory();
    if (!object || object->className() != type.className || object->typeId() != type.id ||
        object->isInTree() || object->kind() != findElementKind(entry.declaration.baseType) ||
        !object->isOwnerThread())
    {
        error = "source control factory returned an incompatible object";
        return {};
    }
    return object;
}
} // namespace velora
