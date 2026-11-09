#pragma once

#include <memory>
#include <velora/controls/UIElement.hpp>
#include <velora/runtime/Framework.hpp>

// Creating controls as objects, before and independent of any UI tree (R05, R02.4 completed).
//
// An element made by createElement has its type's defaults (DefaultStyle layer) and can be
// configured through elementProperties() with no XamlRoot, window or Framework. Inserting it into a
// root (Framework::insert) keeps every value: property ids are derived from their names, so the
// process-wide descriptors here and each Framework's own descriptors name the same properties.

namespace velora
{

struct ElementProperties
{
    ElementProperties();
    ElementProperties(const ElementProperties&) = delete; // descriptors point into the registry
    ElementProperties& operator=(const ElementProperties&) = delete;

    PropertyRegistry registry;
    VisualProperties visual;
    Framework::Properties props;
};

// The schema and runtime-state properties, defined once per process by Framework::defineProperties.
[[nodiscard]] const ElementProperties& elementProperties();

// A new element of `kind` with the type's defaults in the DefaultStyle layer.
[[nodiscard]] std::shared_ptr<UIElement> createElement(ElementKind kind);

} // namespace velora
