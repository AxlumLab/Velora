#pragma once

#include <velora/runtime/object/RuntimeTypeRegistry.hpp>

// The built-in control types in the RuntimeTypeRegistry (R05.1): every schema type plus the abstract
// bases of UWP's hierarchy, so isA answers ("Velora.UI.CheckBox" isA "Velora.UI.ButtonBase").
// Registered after registerBuiltinTypes (it extends Velora.UI.Control/FrameworkElement).
//
// The entries are abstract here: elements are created by createElement (ElementFactory.hpp); the
// XAML loader (R07) adds the activation path for loading by name.

namespace velora
{

RegistrationResult registerControlTypes(RuntimeTypeRegistry& registry);

} // namespace velora
