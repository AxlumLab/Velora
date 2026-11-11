// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
#include "FluentTypes.hpp"
#include <cmath>
#include <stdexcept>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/FluentElement.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>
namespace velora
{
namespace
{
const XamlTypeCatalog& fluentCatalog()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return catalog;
}
} // namespace
const XamlTypeDeclaration& fluentDeclaration(std::string_view name)
{
    const auto* type = fluentCatalog().find(name);
    if (!type)
        throw std::invalid_argument("Unknown Fluent source type");
    return *type;
}
const PropertyDescriptor& fluentProperty(std::string_view type, std::string_view name)
{
    static const auto registry = []
    {
        PropertyRegistry result;
        for (const auto& type : fluentCatalog().types())
            for (const auto& p : type.properties)
            {
                const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
                result.define(p.runtimeName, std::get<PropertyValue>(parseValue(schema, p.defaultValue)),
                              PropertyFlags::kPaint);
            }
        return result;
    }();
    const auto* declaration = fluentDeclaration(type).property(name);
    if (!declaration)
        throw std::invalid_argument("Unknown Fluent source property");
    return *registry.find(declaration->runtimeName);
}
std::string registerFluentControl(Framework& ui, std::string_view name,
                                  SourceControlRegistry::Factory factory)
{
    const auto& type = fluentDeclaration(name);
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : type.properties)
        properties.push_back(&fluentProperty(name, p.name));
    return ui.registerSourceControl(type, std::move(factory), properties);
}
FluentElement::FluentElement(std::string_view type, std::string_view view)
    : UIElement(ElementKind::kUserControl), m_type(type), m_view(view)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor&, const PropertyValue&, const PropertyValue&)
        {
            if (m_ui)
                syncParts();
        });
}
std::string FluentElement::className() const
{
    return m_type;
}
const PropertyDescriptor& FluentElement::property(std::string_view name) const
{
    return fluentProperty(m_type, name);
}
ElementId FluentElement::part(std::string_view name) const
{
    const auto found = m_parts.find(std::string(name));
    return found == m_parts.end() ? 0 : found->second;
}
void FluentElement::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/" + m_view + "View.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error(m_type + ": " + loaded.error);
    m_root = ui.tree().element(*loaded.root);
    for (const auto& [name, object] : loaded.names)
        m_parts.emplace(name, ui.tree().element(*object));
    ui.applyTemplate(self, m_root, {});
}
void FluentElement::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_root = 0;
    m_parts.clear();
    m_events.clear();
}
void FluentElement::bindClick(std::string_view name, std::function<void(FluentElement&)> handler)
{
    const auto weak = std::weak_ptr<DependencyObject>(m_ui->tree().object(m_element));
    m_events.push_back(m_ui->subscribeEvent(part(name), m_ui->events().Click,
                                            [weak, handler = std::move(handler)](ElementId, RoutedEventArgs&)
                                            {
                                                if (const auto owner = weak.lock())
                                                    handler(static_cast<FluentElement&>(*owner));
                                            }));
}
bool FluentElement::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    for (const auto& declaration : fluentDeclaration(m_type).properties)
    {
        if (declaration.id() != p.id)
            continue;
        if (declaration.kind == ValueKind::kBoolean)
            return value.isBool();
        if (declaration.kind == ValueKind::kInteger)
            return value.isInt();
        if (declaration.kind == ValueKind::kDouble)
            return value.isDouble() && std::isfinite(value.asDouble());
        if (!value.isString())
            return false;
        try
        {
            utf8ToUtf16(value.asString());
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return true;
}
} // namespace velora
