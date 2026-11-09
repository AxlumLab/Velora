#pragma once

#include <vector>
#include <velora/reflect/Metadata.hpp>
#include <velora/runtime/ElementTree.hpp>

// Turns reflected rkproperty/rkevent metadata into UI property and event descriptors.
// The UI semantics (flags, value sources, kinds) live here, not in velora.reflect.
namespace velora
{

struct RegisteredType
{
    std::vector<const PropertyDescriptor*> properties;
    std::vector<const EventDescriptor*> events;
    const PropertyDescriptor* content = nullptr; // the Content property, if any
};

// Registers every property and event of `type` under its qualified name. The default value
// is parsed from the rkproperty default= text according to the property type.
RegisteredType registerType(PropertyRegistry& properties, EventRegistry& events, const TypeMetadata& type);
PropertyValue parseDefault(std::string_view type, std::string_view text);

} // namespace velora
