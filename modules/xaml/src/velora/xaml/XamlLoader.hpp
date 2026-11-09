#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <velora/runtime/object/RuntimeTypeRegistry.hpp>
#include <velora/xaml/XamlIR.hpp>

// XamlLoader (R07, R09): replays compiled XAML (.vxbf) into a UI tree through the object runtime.
//
// The loader never names a concrete control. It checks each type against the RuntimeTypeRegistry
// (a type without reflection metadata is an error, R07.1) and asks an XamlLoadTarget — implemented
// by the controls module for a root's UI context — to create objects and set values. Objects are
// opaque handles to it.
//
// Resources, styles and templates (R09): a Resources block is handed to the target whole, which
// builds its dictionary from it; a ControlTemplate is never instantiated while loading a page — the
// target keeps the IR and calls instantiateTemplate for each control that uses the template, each
// time producing an independent tree with its own name scope. After an object's instructions, the
// loader applies its explicit or implicit style, then its explicit template.
//
// Loading is a transaction (R07.5): on the first failure the target discards everything created by
// this load, the existing root keeps its previous state, and the result names the file, line and
// column and the object/property involved.

namespace velora
{

using XamlHandle = u64; // the target's identity of a created object; 0 is none

// {Binding} options beyond Path and Mode (R09.3).
struct XamlBindingOptions
{
    std::string elementName;
    std::optional<XamlRelativeSource> relativeSource;
    std::string converter; // resource key of an IValueConverter
    std::string converterParameter;
    std::optional<PropertyValue> fallback;
};

class XamlLoadTarget
{
public:
    // The fingerprint of the element schema this target implements (see schemaFingerprint).
    [[nodiscard]] virtual u64 schemaFingerprint() const = 0;
    // A new object of `type` as a child of `parent` (0: top level). 0 on failure, with `error` set.
    virtual XamlHandle create(const XamlIRType& type, XamlHandle parent, std::string& error) = 0;
    // Whether `existing` is an object of `type`, for loading into an existing root.
    [[nodiscard]] virtual bool isOfType(XamlHandle existing, const XamlIRType& type) const = 0;
    // Each returns an empty string on success, else the reason.
    virtual std::string setValue(XamlHandle object, const XamlIRProperty& property,
                                 const PropertyValue& value) = 0;
    // `source` is the element an ElementName/RelativeSource binding reads (its property named by
    // `path`); 0 binds to the DataContext.
    virtual std::string setBinding(XamlHandle object, const XamlIRProperty& property, std::string_view path,
                                   u32 mode, const XamlBindingOptions& options, XamlHandle source) = 0;
    virtual std::string setResource(XamlHandle object, const XamlIRProperty& property, std::string_view key,
                                    bool theme) = 0;
    virtual std::string addEvent(XamlHandle object, std::string_view event,
                                 std::function<void()> handler) = 0;
    // Removes an object created by this load, with its subtree (rollback).
    virtual void discard(XamlHandle object) noexcept = 0;
    // The local value of a property (nullopt: unset), and putting it back, so a failed load into an
    // existing root restores the root's own values (rollback).
    [[nodiscard]] virtual std::optional<PropertyValue> localValue(XamlHandle object,
                                                                  const XamlIRProperty& property) const = 0;
    virtual void restoreLocal(XamlHandle object, const XamlIRProperty& property,
                              const std::optional<PropertyValue>& value) noexcept = 0;

    // ---- R09: x:Bind, x:Uid, resources, styles, templates ----------------------------------------
    // {x:Bind}: the code-behind's accessor number `index` drives `property` (BindingMode `mode`).
    virtual std::string setXBind(XamlHandle object, const XamlIRProperty& property, u32 index, u32 mode) = 0;
    // ListView items (R09.5): the item template by key or inline (instruction `begin` is its
    // kBeginDataTemplate), and the ItemsSource from the code-behind's x:Bind accessor `index`.
    virtual std::string setItemTemplate(XamlHandle object, std::string_view key) = 0;
    virtual std::string setItemTemplate(XamlHandle object, const std::shared_ptr<const XamlIR>& ir,
                                        usize begin) = 0;
    virtual std::string setItemsSource(XamlHandle object, u32 index, u32 mode) = 0;
    // x:Bind in a DataTemplate: the code-behind's item accessor `index` against the container's item.
    virtual std::string setItemXBind(XamlHandle object, const XamlIRProperty& property, u32 index,
                                     u32 mode) = 0;
    // x:Uid: every string resource "<uid>.<Property>" for the loading context sets that property.
    virtual std::string applyUid(XamlHandle object, std::string_view uid) = 0;
    // VisualStateManager.VisualStateGroups of `object`: instructions [begin, end] of `ir`. Target
    // names resolve in `names` (the page's or the template instance's scope); `owner` is the
    // control whose states they are (the templated control inside a template, else `object`).
    virtual std::string setVisualStates(XamlHandle object, XamlHandle owner, const XamlIR& ir, usize begin,
                                        usize end,
                                        const std::unordered_map<std::string, XamlHandle>& names) = 0;
    // The object's Resources: instructions [begin, end] of `ir` (kBeginResources .. kEndResources).
    // `ir` is shared because the dictionary's templates replay it later.
    virtual std::string setResources(XamlHandle object, const std::shared_ptr<const XamlIR>& ir, usize begin,
                                     usize end) = 0;
    // Undoes setResources on an existing root (rollback).
    virtual void clearResources(XamlHandle object) noexcept = 0;
    // Style="{StaticResource key}"; an unknown key or a style for another type is an error.
    virtual std::string applyStyle(XamlHandle object, std::string_view key) = 0;
    // The implicit style (keyed by the object's type) in scope, if there is one.
    virtual std::string applyImplicitStyle(XamlHandle object) = 0;
    // Template="{StaticResource key}", or an inline template (instruction `begin` is its
    // kBeginTemplate).
    virtual std::string applyTemplate(XamlHandle object, std::string_view key) = 0;
    virtual std::string applyTemplate(XamlHandle object, const std::shared_ptr<const XamlIR>& ir,
                                      usize begin) = 0;
    // {TemplateBinding}: `object`'s property follows the templated parent's `ownerProperty`.
    virtual std::string templateBind(XamlHandle object, const XamlIRProperty& property, XamlHandle owner,
                                     const XamlIRProperty& ownerProperty) = 0;
    // A template instance is complete: `root` is its tree, `names` its own name scope.
    virtual void completeTemplate(XamlHandle owner, XamlHandle root,
                                  const std::unordered_map<std::string, XamlHandle>& names) = 0;

protected:
    ~XamlLoadTarget() = default;
};

// Maps x:Class event handler names to callables; generated code-behind provides it. Returns an empty
// function for an unknown handler.
using XamlEventConnector = std::function<std::function<void()>(std::string_view handler)>;

struct XamlLoadResult
{
    bool ok = false;
    std::string error; // "<file>:<line>:<column>: <message>"
    XamlHandle root = 0;
    std::unordered_map<std::string, XamlHandle> names; // the page's NameScope
};

struct XamlLoadOptions
{
    // Load into this existing object instead of creating the root (loadComponent on an x:Class
    // instance, R07.2). It must be of the root's type. 0 creates the root.
    XamlHandle existingRoot = 0;
    XamlHandle parent = 0; // where a created root goes (0: top level)
    XamlEventConnector connector;
    // Used instead of `connector` when set: a handler that knows the element it is attached to (item
    // templates pass the element's current item to the controller).
    std::function<std::function<void()>(std::string_view handler, XamlHandle element)> elementConnector;
};

class XamlLoader
{
public:
    explicit XamlLoader(const RuntimeTypeRegistry& types) noexcept : m_types(types) {}

    // Reads and verifies the bytes (against the target's schema), then loads them.
    XamlLoadResult load(std::span<const u8> vxbf, XamlLoadTarget& target,
                        const XamlLoadOptions& options = {});
    // Loads an already-read IR (validate() must hold). A ResourceDictionary file is not loadable as
    // a component; it is merged into a dictionary.
    XamlLoadResult load(const std::shared_ptr<const XamlIR>& ir, XamlLoadTarget& target,
                        const XamlLoadOptions& options = {});
    // One instance of the template whose kBeginTemplate is instruction `begin`, created under
    // `owner` (the templated control). On failure nothing of the instance is left.
    XamlLoadResult instantiateTemplate(const std::shared_ptr<const XamlIR>& ir, usize begin,
                                       XamlLoadTarget& target, XamlHandle owner);
    // One container of the DataTemplate whose kBeginDataTemplate is instruction `begin`, under
    // `parent`; its names form their own scope (result.names), nothing is registered as a template.
    XamlLoadResult instantiateDataTemplate(const std::shared_ptr<const XamlIR>& ir, usize begin,
                                           XamlLoadTarget& target, const XamlLoadOptions& options);

private:
    XamlLoadResult run(const std::shared_ptr<const XamlIR>& ir, usize first, usize last,
                       XamlLoadTarget& target, const XamlLoadOptions& options, XamlHandle templatedParent);

    const RuntimeTypeRegistry& m_types;
};

// The index of the instruction closing the block opened at `begin` (kBeginResources, kBeginStyle,
// kBeginTemplate, kBeginThemeDictionary or kBeginObject). The IR must be valid.
[[nodiscard]] usize xamlBlockEnd(const XamlIR& ir, usize begin);

} // namespace velora
