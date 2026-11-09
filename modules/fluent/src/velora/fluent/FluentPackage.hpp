#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <velora/resources/ResourceProvider.hpp>

// The Fluent framework package (R11.1).
//
// Velora.Fluent.vpak holds the WinUI 2.3 theme dictionaries, default styles and control templates
// compiled to .vxbf under Themes/ (Themes/WinUIButton.vxbf, Themes/WinUITextBox.vxbf,
// Themes/SystemResources.vxbf, Themes/WinUICornerRadius.vxbf). Mounted in the framework namespace,
// a page merges them with Source="ms-appx-framework:///Themes/WinUIButton.xaml"; the dictionaries
// merge each other inside the package. Anything the application defines itself — an implicit
// Button style, a brush key — is found before the merged framework dictionary, so the app
// overrides Fluent without touching control code.

namespace velora
{

inline constexpr std::string_view kFluentPackageName = "Velora.Fluent.vpak";

// Opens and verifies the package and mounts it for ResourceNamespace::kFramework. An empty string
// on success, else why the package was refused.
[[nodiscard]] std::string mountFluent(const std::filesystem::path& package,
                                      ResourceManager& manager = ResourceManager::instance());

} // namespace velora
