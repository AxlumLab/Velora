#pragma once
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <velora/runtime/BindingEngine.hpp>
#include <velora/runtime/ElementTree.hpp>
namespace velora
{
class Framework;
// Theme index: 0 Light, 1 Dark, 2 HighContrast. A missing high contrast value falls back to dark.
struct ResourceValue
{
    std::array<PropertyValue, 2> values;
    PropertyValue highContrast;
    const PropertyValue& at(std::size_t theme) const noexcept
    {
        return theme == 2 ? (highContrast.empty() ? values[1] : highContrast) : values[theme];
    }
};
struct ResourceSetter
{
    std::string property;
    std::array<PropertyValue, 2> values;
    bool live = false;
    PropertyValue highContrast;
    const PropertyValue& at(std::size_t theme) const noexcept
    {
        return theme == 2 ? (highContrast.empty() ? values[1] : highContrast) : values[theme];
    }
};
using ResourceTemplateFactory = std::function<std::shared_ptr<void>(Framework&, ElementId)>;
struct ResourceStyle
{
    std::string target;
    std::string basedOn;
    std::vector<ResourceSetter> setters;
    // Invalid or unsupported definitions are rejected before changing the current style.
    bool supported = true;
    ResourceTemplateFactory templateFactory;
};
// A keyed ControlTemplate (R09.5): instantiated per control that uses it.
struct ResourceTemplate
{
    std::string target;
    ResourceTemplateFactory factory;
};
struct ResourceDictionary
{
    std::map<std::string, ResourceValue, std::less<>> values;
    std::map<std::string, ResourceStyle, std::less<>> styles;
    std::map<std::string, ResourceTemplate, std::less<>> templates;
    // DataTemplates: the factory creates one item container under the given parent and returns a
    // shared ElementId of its root.
    std::map<std::string, ResourceTemplateFactory, std::less<>> dataTemplates;
    std::map<std::string, std::shared_ptr<const IValueConverter>, std::less<>> converters;
    std::vector<std::shared_ptr<ResourceDictionary>> merged;
    std::array<std::shared_ptr<ResourceDictionary>, 2> themes;
    std::shared_ptr<ResourceDictionary> defaultTheme;
    std::shared_ptr<ResourceDictionary> highContrastTheme;
};
// Framework-owned native resources; scopes are attached to actual logical elements.
class ResourceScopeStore
{
public:
    explicit ResourceScopeStore(Framework& framework) : m_framework(framework) {}
    ResourceScopeStore(const ResourceScopeStore&) = delete;
    ResourceScopeStore& operator=(const ResourceScopeStore&) = delete;
    bool setScope(ElementId element, ResourceDictionary dictionary);
    // Removes the element's own dictionary (a failed load into an existing root).
    void clearScope(ElementId element);
    // Whether a style is found under `key` from `element`'s scope (an implicit style's key is the
    // target type name).
    bool hasStyle(ElementId element, std::string_view key);
    // Template="{StaticResource key}" or an inline template: replaces the element's template. An
    // explicit template outranks a style's template, also when the style is applied again later.
    bool applyTemplate(ElementId element, std::string_view key);
    void applyTemplate(ElementId element, const ResourceTemplateFactory& factory);
    void setApplication(ResourceDictionary dictionary);
    std::optional<PropertyValue> find(ElementId element, std::string_view key);
    // A value converter resource (Converter={StaticResource key}).
    std::shared_ptr<const IValueConverter> findConverter(ElementId element, std::string_view key);
    ResourceTemplateFactory findDataTemplate(ElementId element, std::string_view key);
    // The value under each theme [Light, Dark, HighContrast], for a ThemeResource that follows theme
    // changes. The high contrast slot is empty when no dictionary has a high contrast value.
    std::optional<std::array<PropertyValue, 3>> findPerTheme(ElementId element, std::string_view key);
    bool applyStyle(ElementId element, std::string_view key);
    // Generated lowering records its setters so later native replacement clears them too.
    void recordStyleSetter(ElementId element, const PropertyDescriptor* property);
    void prune();
    std::size_t scopeCount()
    {
        prune();
        return m_scopes.size();
    }

private:
    struct StyleLocation
    {
        const ResourceStyle* style;
        const ResourceDictionary* dictionary;
        ElementId owner;
    };
    std::optional<StyleLocation> findStyle(ElementId element, std::string_view key);
    bool collectStyle(StyleLocation location, std::vector<const ResourceStyle*>& chain,
                      std::vector<ResourceSetter>& setters, ResourceTemplateFactory& factory);
    void instantiate(ElementId element, const ResourceTemplateFactory& factory);
    Framework& m_framework;
    std::unordered_map<ElementId, ResourceDictionary> m_scopes;
    ResourceDictionary m_application;
    std::unordered_map<ElementId, std::vector<const PropertyDescriptor*>> m_applied;
    std::unordered_map<ElementId, std::shared_ptr<void>> m_templateInstances;
    std::unordered_set<ElementId> m_explicitTemplates;
    std::shared_ptr<void> m_lifetime = std::make_shared<int>(0);
};
}
