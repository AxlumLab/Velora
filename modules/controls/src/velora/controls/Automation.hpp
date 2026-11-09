#pragma once

#include <optional>
#include <string>
#include <vector>
#include <velora/runtime/Framework.hpp>

// Automation (R10.5): the accessibility view of a UI context, the layer a platform provider (UI
// Automation on Windows) exposes to screen readers and test tools.
//
// The tree is the logical tree with each element's control type, name and state. A templated
// control is one node: its template parts are its presentation, not separate controls (the
// UIA "content view"). Patterns act through the controls' own input paths, so automation
// behaves exactly like a user: Invoke and Toggle are the Enter key on the focused control,
// RangeValue and Value set the property the way the control's own input does.
//
// Not yet here: the Win32 UIA provider (WM_GETOBJECT) and AutomationProperties in XAML; the
// name falls back to the element's own text (Content, Text, Header) or x:Name-free empty.

namespace velora
{

enum class AutomationControlType : u8
{
    kPane,
    kButton,
    kCheckBox,
    kRadioButton,
    kEdit,
    kText,
    kSlider,
    kProgressBar,
    kList,
    kScrollViewer,
    kImage,
    kGroup,
};

struct AutomationNode
{
    ElementId element = 0;
    AutomationControlType type = AutomationControlType::kGroup;
    std::string name;
    bool enabled = true;
    bool focusable = false;
    bool focused = false;
    std::optional<bool> toggled;                   // Toggle pattern
    std::optional<double> value, minimum, maximum; // RangeValue pattern
    std::optional<std::string> text;               // Value pattern (TextBox)
    Box bounds;
    std::vector<AutomationNode> children;
};

[[nodiscard]] AutomationControlType automationType(ElementKind kind) noexcept;
// The automation tree under `root` (a visual root of `ui`); hidden elements are left out.
[[nodiscard]] AutomationNode automationTree(Framework& ui, ElementId root);

// Patterns; false when the element does not support the pattern or is disabled.
bool automationInvoke(Framework& ui, ElementId element);
bool automationToggle(Framework& ui, ElementId element);
bool automationSetRangeValue(Framework& ui, ElementId element, double value);
bool automationSetValue(Framework& ui, ElementId element, std::string_view text);

} // namespace velora
