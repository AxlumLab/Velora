#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <velora/controls/UIElement.hpp>
#include <velora/controls/XBind.hpp>
#include <velora/resources/ResourceContext.hpp>
#include <velora/resources/ResourceProvider.hpp>
#include <velora/resources/ResourceUri.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/xaml/XamlLoader.hpp>

// Loading compiled XAML into a root's UI context (R07.2/R07.6): the controls side of XamlLoader.
//
// Generated code-behind (velorac `vxbf`) calls loadComponent with its page's resource URI; the
// bytes come from the ResourceManager (velorac embeds them in the executable). Nothing about the
// page's structure is in the generated C++ — it holds the controller wiring only.

namespace velora
{

// Every built-in and control type, registered once (registerBuiltinTypes + registerControlTypes).
[[nodiscard]] const RuntimeTypeRegistry& controlTypeRegistry();

struct ComponentLoad
{
    bool ok = false;
    std::string error;
    std::shared_ptr<UIElement> root;
    std::unordered_map<std::string, std::shared_ptr<UIElement>> names; // the page's NameScope
};

struct ComponentLoadOptions
{
    // Load into this existing element (an x:Class instance) instead of creating the root. It must
    // already be in `ui`'s tree and be of the page's root type.
    UIElement* existingRoot = nullptr;
    // Where a created root goes; null makes it a top-level element.
    UIElement* parent = nullptr;
    XamlEventConnector connector;
    XBindConnector xbind; // the generated code-behind's x:Bind accessors
    // Inside DataTemplates (x:DataType): x:Bind accessor `index` for an item, and handlers that
    // receive the container's item (Click="Remove" -> controller.Remove(Person&)).
    std::function<std::optional<XBindAccessor>(u32 index, const ObjectRef& item)> itemXBind;
    std::function<std::function<void(const ObjectRef& item)>(std::string_view handler)> itemConnector;
    // OnApplyTemplate: called for each element of this page whose template is applied, during the
    // load and afterwards (a style change), until the page's root is removed.
    std::function<void(Framework&, ElementId owner)> templateApplied;
    // x:Uid strings come from `resources` for `resourceContext` (the root's language). Defaults:
    // ResourceManager::instance() and a default context.
    const ResourceManager* resources = nullptr;
    ResourceContext resourceContext;
};

// Loads compiled XAML bytes into `ui`.
ComponentLoad loadComponent(Framework& ui, std::span<const u8> vxbf,
                            const ComponentLoadOptions& options = {});
// Loads the compiled XAML stored at `uri` (ResourceManager::instance()).
ComponentLoad loadComponent(Framework& ui, const ResourceUri& uri, const ComponentLoadOptions& options = {});

} // namespace velora
