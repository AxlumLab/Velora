#pragma once

#include "velora/xaml/Ast.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// velorac: checks a XAML file against the built-in framework schema and reflection metadata
// and generates a C++ view class. Build-time only.
//
// For <Page x:Class="Demo.MainPage"> it emits Demo::MainPage_xaml (MainPage.g.hpp/.g.cpp):
//   MainPage_xaml(velora::Framework&, Demo::MainPage& controller, ElementId parent = 0);
// The controller is a user C++ class described by .rkmeta (no partial classes). The view
// builds elements through velora::Framework and calls the controller directly for x:Bind
// paths and event handlers. OneWay/TwoWay x:Bind subscribe to every velora::Observable
// along the path (direct getter/setter calls, no runtime lookup); {Binding} goes through
// BindingEngine against the DataContext. Each ListView DataTemplate becomes a nested
// container class (ItemTemplate<n>) realized and recycled by ItemsRepeater. Destroying
// the view removes its element subtree with every handler, binding and subscription, so no
// callback outlives it. The controller must outlive the view.
namespace velora
{

struct CompileOptions
{
    // .rkmeta files with the header that declares their types, as consumers include it.
    std::vector<std::pair<std::filesystem::path, std::string>> metadata;
    // Other XAML files of the same build; their x:Class views can be used as custom controls and
    // their ResourceDictionary files can be merged by Source.
    std::vector<std::filesystem::path> peers;
    // Application-level ResourceDictionary files (like Application.Resources): the outermost
    // lookup scope of every page and dictionary, after their own resources.
    std::vector<std::filesystem::path> application;
};

struct CompileResult
{
    std::vector<XamlDiagnostic> diagnostics;
    std::string headerName; // <stem>.g.hpp
    std::string header, source;
    std::vector<std::filesystem::path> dependencies; // metadata and peer files actually used
    bool ok() const noexcept
    {
        return diagnostics.empty();
    }
};

CompileResult compile(std::string_view source, const std::filesystem::path& path,
                      const CompileOptions& options);
// Checks a ResourceDictionary file (`velorac resources`): every entry resolves in both themes,
// styles and templates are valid. Produces no code.
CompileResult checkResources(std::string_view source, const std::filesystem::path& path,
                             const CompileOptions& options);

} // namespace velora
