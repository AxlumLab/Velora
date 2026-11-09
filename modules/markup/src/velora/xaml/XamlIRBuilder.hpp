#pragma once

#include <vector>
#include <velora/controls/XamlTypeCatalog.hpp>
#include <velora/reflect/Metadata.hpp>
#include <velora/xaml/Ast.hpp>
#include <velora/xaml/XamlIR.hpp>

// velorac's front half of the .vxbf pipeline (R06.1/R06.2): a parsed XAML document lowered to
// XamlIR, checked against the element schema with diagnostics at source positions.
//
// Supported: built-in element types, literal property values (parsed by the schema exactly as the
// legacy compiler does), attached properties, Grid row/column definitions, text content, x:Name,
// x:Class on the root, event handlers, {Binding}, {StaticResource} and {ThemeResource}; and (R09)
// element Resources, ResourceDictionary files, MergedDictionaries, Light/Dark/Default theme
// dictionaries, keyed and implicit Styles with BasedOn, ControlTemplates (keyed, as a Template setter
// or inline) with {TemplateBinding} and their own name scopes, Style= and Template= references.
// x:Bind, visual states, HighContrast and DataTemplates are also supported below. Source controls
// use the optional declaration catalog for the initial literal-property slice; unsupported
// extension semantics are explicit diagnostics, never dropped.

namespace velora
{

// One {x:Bind}, resolved against the x:Class's reflection metadata: the code-behind writer turns it
// into the accessor the .vxbf's kSetXBind (by ordinal) refers to.
struct XamlXBind
{
    std::string path;
    std::vector<BindingStep> steps;
    u32 mode = 0;         // BindingMode: OneTime is the x:Bind default
    std::string itemType; // inside a DataTemplate: the C++ item type (x:DataType); empty: the x:Class
};

struct XamlIRResult
{
    XamlIR ir;
    std::vector<XamlXBind> xbinds;
    // Event handlers inside DataTemplates and the C++ item type each receives.
    std::vector<std::pair<std::string, std::string>> itemHandlers;
    std::vector<XamlDiagnostic> diagnostics;
    bool ok() const noexcept
    {
        return diagnostics.empty();
    }
};

// `catalog` holds the reflection metadata of the x:Class (and the types its x:Bind paths reach);
// without it, x:Bind is a diagnostic.
[[nodiscard]] XamlIRResult buildXamlIR(const XamlDocument& document, std::string_view sourceName,
                                       const MetadataCatalog* catalog = nullptr,
                                       const XamlTypeCatalog* sourceTypes = nullptr);

} // namespace velora
