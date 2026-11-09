#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <velora/runtime/FrameworkSchema.hpp>

namespace velora
{

struct XamlPropertyDeclaration
{
    std::string name;
    ValueKind kind = ValueKind::kDouble;
    std::string runtimeName;
    std::string defaultValue;
    [[nodiscard]] u64 id() const;
};

struct XamlTypeDeclaration
{
    std::string className;
    std::string baseType;
    std::vector<XamlPropertyDeclaration> properties;
    [[nodiscard]] u64 contract() const;
    [[nodiscard]] const XamlPropertyDeclaration* property(std::string_view name) const;
};

// Versioned, declarative source-control metadata. No factories or process addresses in the file.
// append() validates a complete batch before publication; existing declarations remain intact
// on failure. First slice: built-in bases and primitive, paint-affecting properties.
class XamlTypeCatalog final
{
  public:
    std::string append(std::string_view text);
    [[nodiscard]] const XamlTypeDeclaration* find(std::string_view className) const;
    [[nodiscard]] std::span<const XamlTypeDeclaration> types() const noexcept
    {
        return m_types;
    }

  private:
    std::vector<XamlTypeDeclaration> m_types;
};
} // namespace velora
