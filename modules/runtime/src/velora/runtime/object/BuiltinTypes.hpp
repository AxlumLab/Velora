#pragma once

#include <velora/runtime/object/RuntimeTypeRegistry.hpp>

namespace velora
{

inline constexpr const char* kObjectClassName = "Velora.Object";
inline constexpr const char* kDependencyObjectClassName = "Velora.UI.DependencyObject";
inline constexpr const char* kUIElementClassName = "Velora.UI.UIElement";
inline constexpr const char* kFrameworkElementClassName = "Velora.UI.FrameworkElement";
inline constexpr const char* kControlClassName = "Velora.UI.Control";

// Registers the base type metadata. Concrete controls add their own descriptions in controls.
RegistrationResult registerBuiltinTypes(RuntimeTypeRegistry& registry);

} // namespace velora
