#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/runtime/object/RuntimeTypeRegistry.hpp>
#include <velora/xaml/XamlIR.hpp>

namespace velora
{
// Per-Framework construction bindings for declarative source types. One runtime/tree/property
// store remains in use. This is source registration, not a binary plugin loader.
class SourceControlRegistry final
{
  public:
    using Factory = std::function<std::shared_ptr<UIElement>()>;
    SourceControlRegistry();
    std::string add(const XamlTypeDeclaration&, Factory,
                    std::span<const PropertyDescriptor* const> properties);
    [[nodiscard]] const RuntimeTypeRegistry& types() const noexcept
    {
        return m_types;
    }
    [[nodiscard]] const PropertyDescriptor* property(std::string_view qualifiedName) const;
    [[nodiscard]] bool ownsProperty(std::string_view className, std::string_view qualifiedName) const;
    [[nodiscard]] bool contains(std::string_view className) const;
    [[nodiscard]] bool matches(const XamlIRType& type) const;
    std::shared_ptr<UIElement> create(const XamlIRType&, std::string& error) const;

  private:
    struct Entry
    {
        XamlTypeDeclaration declaration;
        Factory factory;
        std::unordered_map<std::string, const PropertyDescriptor*> properties;
    };
    RuntimeTypeRegistry m_types;
    std::unordered_map<std::string, Entry> m_entries;
};
} // namespace velora
