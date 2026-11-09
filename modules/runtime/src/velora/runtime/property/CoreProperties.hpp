#pragma once

#include <velora/runtime/property/PropertyTypes.hpp>

// The core UIElement/FrameworkElement/Control properties as process-wide dependency properties
// (R03.2), for objects that live outside a Framework.
//
// Each has the same owner, name, default and flags as the property Framework registers for the
// same XAML name, so both get the same stable id (propertyIdFor) and a value written through one
// is the same property as through the other. velora_dependency_property_test checks the match.
//
// XAML "Visibility" is stored as the boolean "IsVisible" (true = Visible), as in Framework.

namespace velora
{

struct CoreProperties
{
    const PropertyDescriptor* isVisible = nullptr;
    const PropertyDescriptor* opacity = nullptr;
    const PropertyDescriptor* width = nullptr;
    const PropertyDescriptor* isEnabled = nullptr;
    const PropertyDescriptor* content = nullptr;
};

// Registered on first use; thread-safe initialization, immutable afterwards.
[[nodiscard]] const CoreProperties& coreProperties();

} // namespace velora
