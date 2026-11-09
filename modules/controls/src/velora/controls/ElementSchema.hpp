#pragma once

#include <string_view>
#include <velora/runtime/property/PropertyTypes.hpp>

// The element schema as compiled XAML sees it (R06): XAML property names mapped to the runtime
// properties they set, and a fingerprint of the whole schema. velorac writes property ids and the
// fingerprint into .vxbf; the loader resolves the ids and refuses files built for another schema.

namespace velora
{

// The runtime property a XAML property name sets ("Visibility" sets IsVisible), or null.
[[nodiscard]] const PropertyDescriptor* schemaPropertyDescriptor(std::string_view xamlName);

// FNV-1a over every type (name, content model, properties, events) and property (name, kind,
// default, flags, runtime id) of the schema. Changes whenever compiled XAML could mean something else.
[[nodiscard]] u64 schemaFingerprint() noexcept;

} // namespace velora
