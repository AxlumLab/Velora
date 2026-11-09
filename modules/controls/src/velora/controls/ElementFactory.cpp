#include <velora/controls/ElementFactory.hpp>
#include <velora/runtime/SchemaValue.hpp>

namespace velora
{

ElementProperties::ElementProperties() : visual(VisualProperties::define(registry))
{
    Framework::defineProperties(registry, visual, props);
}

const ElementProperties& elementProperties()
{
    static const ElementProperties properties;
    return properties;
}

std::shared_ptr<UIElement> createElement(ElementKind kind)
{
    auto element = createElementObject(kind);
    detail::applyElementDefaults(*element);
    return element;
}

void detail::applyElementDefaults(UIElement& element)
{
    if (!element.kind())
        return;
    const ElementKind kind = *element.kind();
    const auto& properties = elementProperties();
    const SchemaType& type = schemaType(kind);
    // The type's built-in look is its default style (R03.3): below application Style setters.
    for (const auto& d : type.defaults)
    {
        auto value = parseValue(*findSchemaProperty(d.property), d.value);
        const auto* descriptor = properties.registry.find(d.property);
        if (descriptor && std::holds_alternative<PropertyValue>(value))
            element.setValue(*descriptor, std::get<PropertyValue>(std::move(value)),
                              PropertySource::kDefaultStyle);
    }
    if (kind == ElementKind::kScrollViewer || type.items)
        element.setValue(*properties.visual.clipToBounds, PropertyValue(true),
                          PropertySource::kDefaultStyle);
}

} // namespace velora
