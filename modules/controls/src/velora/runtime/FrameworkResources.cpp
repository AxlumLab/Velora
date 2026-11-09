#include <algorithm>
#include <unordered_set>
#include <velora/runtime/Framework.hpp>
#include <velora/runtime/FrameworkResources.hpp>
#include <velora/runtime/SchemaValue.hpp>
namespace velora
{
namespace
{
template <class T, class Map>
const T* searchDictionary(const ResourceDictionary& dictionary, std::string_view key, std::size_t theme,
                          Map map, std::unordered_set<const ResourceDictionary*>& visited,
                          const ResourceDictionary** home = nullptr)
{
    if (!visited.insert(&dictionary).second)
        return nullptr;
    const auto& entries = map(dictionary);
    if (const auto it = entries.find(key); it != entries.end())
    {
        if (home)
            *home = &dictionary;
        return &it->second;
    }
    for (auto it = dictionary.merged.rbegin(); it != dictionary.merged.rend(); ++it)
        if (*it)
            if (const auto* found = searchDictionary<T>(**it, key, theme, map, visited, home))
                return found;
    // High contrast: its own dictionary, else Default, else Dark.
    const auto selected = theme == 2 ? (dictionary.highContrastTheme ? dictionary.highContrastTheme
                                        : dictionary.defaultTheme    ? dictionary.defaultTheme
                                                                     : dictionary.themes[1])
                          : dictionary.themes[theme] ? dictionary.themes[theme]
                                                     : dictionary.defaultTheme;
    return selected ? searchDictionary<T>(*selected, key, theme, map, visited, home) : nullptr;
}
const auto values = [](const ResourceDictionary& d) -> const auto& { return d.values; };
const auto styles = [](const ResourceDictionary& d) -> const auto& { return d.styles; };
const auto templates = [](const ResourceDictionary& d) -> const auto& { return d.templates; };
const auto converters = [](const ResourceDictionary& d) -> const auto& { return d.converters; };
const auto dataTemplates = [](const ResourceDictionary& d) -> const auto& { return d.dataTemplates; };
bool compatible(const PropertyValue& value, const SchemaProperty& property)
{
    if (value.empty())
        return true;
    switch (property.kind)
    {
    case ValueKind::kString:
        return value.isString();
    case ValueKind::kInteger:
    case ValueKind::kBrush:
        return value.isInt();
    case ValueKind::kDouble:
    case ValueKind::kCornerRadius:
        return value.isDouble() || value.isInt();
    case ValueKind::kBoolean:
    case ValueKind::kVisibility:
        return value.isBool();
    case ValueKind::kThickness:
        return value.isThickness();
    case ValueKind::kGridLengths:
        return value.isGridLengths();
    case ValueKind::kObject:
        return value.isObject();
    case ValueKind::kEnum:
        return value.isString() &&
               std::holds_alternative<PropertyValue>(parseValue(property, value.asString()));
    }
    return false;
}
}
void ResourceScopeStore::prune()
{
    std::erase_if(m_scopes, [&](const auto& entry) { return !m_framework.tree().contains(entry.first); });
    std::erase_if(m_applied, [&](const auto& entry) { return !m_framework.tree().contains(entry.first); });
    std::erase_if(m_explicitTemplates, [&](ElementId e) { return !m_framework.tree().contains(e); });
}
void ResourceScopeStore::clearScope(ElementId element)
{
    m_scopes.erase(element);
}
bool ResourceScopeStore::hasStyle(ElementId element, std::string_view key)
{
    prune();
    return m_framework.tree().contains(element) && findStyle(element, key).has_value();
}
bool ResourceScopeStore::applyTemplate(ElementId element, std::string_view key)
{
    prune();
    if (!m_framework.tree().contains(element))
        return false;
    const auto theme = static_cast<std::size_t>(m_framework.theme());
    const ResourceTemplate* found = nullptr;
    for (auto current = element; current && !found; current = m_framework.tree().parent(current))
        if (const auto it = m_scopes.find(current); it != m_scopes.end())
        {
            std::unordered_set<const ResourceDictionary*> visited;
            found = searchDictionary<ResourceTemplate>(it->second, key, theme, templates, visited);
        }
    if (!found)
    {
        std::unordered_set<const ResourceDictionary*> visited;
        found = searchDictionary<ResourceTemplate>(m_application, key, theme, templates, visited);
    }
    if (!found || found->target != m_framework.tree().type(element))
        return false;
    applyTemplate(element, found->factory);
    return true;
}
void ResourceScopeStore::applyTemplate(ElementId element, const ResourceTemplateFactory& factory)
{
    m_explicitTemplates.insert(element);
    instantiate(element, factory);
}
void ResourceScopeStore::instantiate(ElementId element, const ResourceTemplateFactory& factory)
{
    m_framework.removeTemplate(element);
    m_templateInstances.erase(element);
    const auto childrenBefore = m_framework.tree().children(element);
    try
    {
        m_templateInstances[element] = factory(m_framework, element);
    }
    catch (...)
    {
        // Constructors can create nodes before throwing, even before applyTemplate registers a root.
        m_framework.removeTemplate(element);
        const auto childrenAfter = m_framework.tree().children(element);
        for (const auto child : childrenAfter)
            if (std::ranges::find(childrenBefore, child) == childrenBefore.end())
                m_framework.tree().remove(child);
        m_templateInstances.erase(element);
        throw;
    }
    std::weak_ptr<void> lifetime = m_lifetime;
    m_framework.tree().onRemoved(element,
                                 [this, element, lifetime]
                                 {
                                     if (!lifetime.expired())
                                     {
                                         m_templateInstances.erase(element);
                                         m_explicitTemplates.erase(element);
                                     }
                                 });
}
bool ResourceScopeStore::setScope(ElementId element, ResourceDictionary dictionary)
{
    prune();
    if (!m_framework.tree().contains(element))
        return false;
    if (!m_scopes.contains(element))
    {
        std::weak_ptr<void> lifetime = m_lifetime;
        m_framework.tree().onRemoved(element,
                                     [this, element, lifetime]
                                     {
                                         if (!lifetime.expired())
                                         {
                                             m_scopes.erase(element);
                                             m_applied.erase(element);
                                         }
                                     });
    }
    m_scopes.insert_or_assign(element, std::move(dictionary));
    return true;
}
void ResourceScopeStore::setApplication(ResourceDictionary dictionary)
{
    m_application = std::move(dictionary);
}
std::optional<PropertyValue> ResourceScopeStore::find(ElementId element, std::string_view key)
{
    prune();
    if (!m_framework.tree().contains(element))
        return std::nullopt;
    const auto theme = static_cast<std::size_t>(m_framework.theme());
    for (auto current = element; current; current = m_framework.tree().parent(current))
        if (const auto it = m_scopes.find(current); it != m_scopes.end())
        {
            std::unordered_set<const ResourceDictionary*> visited;
            if (const auto* value = searchDictionary<ResourceValue>(it->second, key, theme, values, visited))
                return value->at(theme);
        }
    std::unordered_set<const ResourceDictionary*> visited;
    if (const auto* value = searchDictionary<ResourceValue>(m_application, key, theme, values, visited))
        return value->at(theme);
    return std::nullopt;
}
std::shared_ptr<const IValueConverter> ResourceScopeStore::findConverter(ElementId element, std::string_view key)
{
    prune();
    if (!m_framework.tree().contains(element))
        return nullptr;
    const auto theme = static_cast<std::size_t>(m_framework.theme());
    using Entry = std::shared_ptr<const IValueConverter>;
    for (auto current = element; current; current = m_framework.tree().parent(current))
        if (const auto it = m_scopes.find(current); it != m_scopes.end())
        {
            std::unordered_set<const ResourceDictionary*> visited;
            if (const auto* found = searchDictionary<Entry>(it->second, key, theme, converters, visited))
                return *found;
        }
    std::unordered_set<const ResourceDictionary*> visited;
    const auto* found = searchDictionary<Entry>(m_application, key, theme, converters, visited);
    return found ? *found : nullptr;
}
ResourceTemplateFactory ResourceScopeStore::findDataTemplate(ElementId element, std::string_view key)
{
    prune();
    if (!m_framework.tree().contains(element))
        return {};
    const auto theme = static_cast<std::size_t>(m_framework.theme());
    for (auto current = element; current; current = m_framework.tree().parent(current))
        if (const auto it = m_scopes.find(current); it != m_scopes.end())
        {
            std::unordered_set<const ResourceDictionary*> visited;
            if (const auto* found =
                    searchDictionary<ResourceTemplateFactory>(it->second, key, theme, dataTemplates, visited))
                return *found;
        }
    std::unordered_set<const ResourceDictionary*> visited;
    const auto* found = searchDictionary<ResourceTemplateFactory>(m_application, key, theme, dataTemplates, visited);
    return found ? *found : ResourceTemplateFactory{};
}
std::optional<std::array<PropertyValue, 3>> ResourceScopeStore::findPerTheme(ElementId element,
                                                                             std::string_view key)
{
    prune();
    if (!m_framework.tree().contains(element))
        return std::nullopt;
    std::array<PropertyValue, 3> result;
    for (std::size_t theme = 0; theme < result.size(); ++theme)
    {
        const ResourceValue* found = nullptr;
        for (auto current = element; current && !found; current = m_framework.tree().parent(current))
            if (const auto it = m_scopes.find(current); it != m_scopes.end())
            {
                std::unordered_set<const ResourceDictionary*> visited;
                found = searchDictionary<ResourceValue>(it->second, key, theme, values, visited);
            }
        if (!found)
        {
            std::unordered_set<const ResourceDictionary*> visited;
            found = searchDictionary<ResourceValue>(m_application, key, theme, values, visited);
        }
        if (!found && theme == 2)
            continue; // no high contrast value: Themed falls back to dark
        if (!found)
            return std::nullopt;
        result[theme] = found->at(theme);
    }
    return result;
}
std::optional<ResourceScopeStore::StyleLocation> ResourceScopeStore::findStyle(ElementId element,
                                                                               std::string_view key)
{
    const auto theme = static_cast<std::size_t>(m_framework.theme());
    for (auto current = element; current; current = m_framework.tree().parent(current))
        if (const auto it = m_scopes.find(current); it != m_scopes.end())
        {
            std::unordered_set<const ResourceDictionary*> visited;
            const ResourceDictionary* home = nullptr;
            if (const auto* style =
                    searchDictionary<ResourceStyle>(it->second, key, theme, styles, visited, &home))
                return StyleLocation{style, home, current};
        }
    std::unordered_set<const ResourceDictionary*> visited;
    const ResourceDictionary* home = nullptr;
    if (const auto* style =
            searchDictionary<ResourceStyle>(m_application, key, theme, styles, visited, &home))
        return StyleLocation{style, home, 0};
    return std::nullopt;
}
bool ResourceScopeStore::collectStyle(StyleLocation location, std::vector<const ResourceStyle*>& chain,
                                      std::vector<ResourceSetter>& setters, ResourceTemplateFactory& factory)
{
    const auto& style = *location.style;
    if (!style.supported || std::ranges::find(chain, &style) != chain.end())
        return false;
    chain.push_back(&style);
    if (!style.basedOn.empty())
    {
        // BasedOn resolves in the definition scope, never the consuming child override.
        std::unordered_set<const ResourceDictionary*> visited;
        const ResourceDictionary* baseHome = nullptr;
        const auto* base = searchDictionary<ResourceStyle>(*location.dictionary, style.basedOn,
                                                           static_cast<std::size_t>(m_framework.theme()),
                                                           styles, visited, &baseHome);
        auto found =
            base ? std::optional<StyleLocation>(StyleLocation{base, baseHome, location.owner})
                 : findStyle(location.owner ? m_framework.tree().parent(location.owner) : 0, style.basedOn);
        if (!found || found->style->target != style.target || !collectStyle(*found, chain, setters, factory))
            return false;
    }
    for (const auto& setter : style.setters)
    {
        std::erase_if(setters, [&](const auto& existing) { return existing.property == setter.property; });
        setters.push_back(setter);
    }
    if (style.templateFactory)
        factory = style.templateFactory;
    chain.pop_back();
    return true;
}
void ResourceScopeStore::recordStyleSetter(ElementId element, const PropertyDescriptor* property)
{
    if (!property || !m_framework.tree().contains(element))
        return;
    if (!m_applied.contains(element))
    {
        std::weak_ptr<void> lifetime = m_lifetime;
        m_framework.tree().onRemoved(element,
                                     [this, element, lifetime]
                                     {
                                         if (!lifetime.expired())
                                             m_applied.erase(element);
                                     });
    }
    auto& applied = m_applied[element];
    if (std::ranges::find(applied, property) == applied.end())
        applied.push_back(property);
}
bool ResourceScopeStore::applyStyle(ElementId element, std::string_view key)
{
    prune();
    if (!m_framework.tree().contains(element))
        return false;
    const auto found = findStyle(element, key);
    if (!found || found->style->target != m_framework.tree().type(element))
        return false;
    std::vector<const ResourceStyle*> chain;
    std::vector<ResourceSetter> setters;
    ResourceTemplateFactory factory;
    if (!collectStyle(*found, chain, setters, factory))
        return false;
    std::vector<const PropertyDescriptor*> descriptors;
    const auto* type = findSchemaType(m_framework.tree().type(element));
    for (const auto& setter : setters)
    {
        const auto* descriptor = m_framework.property(setter.property);
        const auto* schema = findSchemaProperty(setter.property);
        if (!type || !hasSchemaProperty(*type, setter.property) || !descriptor || !schema ||
            !compatible(setter.values[0], *schema) || !compatible(setter.values[1], *schema))
            return false;
        descriptors.push_back(descriptor);
    }
    // All validation precedes mutations; allocation failures inside user factories can still throw.
    // An explicit template (Template="...") outranks the style's.
    const bool keepTemplate = m_explicitTemplates.contains(element);
    if (!keepTemplate)
    {
        m_framework.removeTemplate(element);
        m_templateInstances.erase(element);
    }
    // Validate the entire BasedOn chain and every setter before mutating the old style.
    if (const auto it = m_applied.find(element); it != m_applied.end())
        for (const auto* descriptor : it->second)
            m_framework.clear(element, descriptor, PropertySource::kStyle);
    for (std::size_t index = 0; index < setters.size(); ++index)
    {
        const auto& setter = setters[index];
        if (setter.live)
            m_framework.setThemed(element, descriptors[index],
                                  Themed{setter.values[0], setter.values[1], setter.highContrast},
                                  PropertySource::kStyle);
        else
            m_framework.set(element, descriptors[index],
                            setter.at(static_cast<std::size_t>(m_framework.theme())),
                            PropertySource::kStyle);
    }
    if (factory && !keepTemplate)
        instantiate(element, factory);
    for (const auto* descriptor : descriptors)
        recordStyleSetter(element, descriptor);
    m_applied.insert_or_assign(element, std::move(descriptors));
    return true;
}
}
