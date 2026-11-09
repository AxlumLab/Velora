#include <velora/controls/Automation.hpp>

namespace velora
{

AutomationControlType automationType(ElementKind kind) noexcept
{
    switch (kind)
    {
    case ElementKind::kPage:
    case ElementKind::kUserControl:
        return AutomationControlType::kPane;
    case ElementKind::kButton:
        return AutomationControlType::kButton;
    case ElementKind::kCheckBox:
    case ElementKind::kToggleSwitch:
        return AutomationControlType::kCheckBox;
    case ElementKind::kRadioButton:
        return AutomationControlType::kRadioButton;
    case ElementKind::kTextBox:
        return AutomationControlType::kEdit;
    case ElementKind::kTextBlock:
        return AutomationControlType::kText;
    case ElementKind::kSlider:
        return AutomationControlType::kSlider;
    case ElementKind::kProgressBar:
        return AutomationControlType::kProgressBar;
    case ElementKind::kListView:
        return AutomationControlType::kList;
    case ElementKind::kScrollViewer:
        return AutomationControlType::kScrollViewer;
    case ElementKind::kImage:
        return AutomationControlType::kImage;
    default:
        return AutomationControlType::kGroup;
    }
}

namespace
{
bool templated(Framework& ui, ElementId e)
{
    return ui.tree().property(e, *ui.props().HasTemplate).isBool() &&
           ui.tree().property(e, *ui.props().HasTemplate).asBool();
}

std::string nameOf(Framework& ui, ElementId e)
{
    const auto& p = ui.props();
    for (const auto* d : {p.Content, p.Text, p.Header})
        if (d)
            if (const auto& v = ui.tree().property(e, *d); v.isString() && !v.asString().empty())
                return v.asString();
    return {};
}

AutomationNode build(Framework& ui, ElementId e)
{
    const auto& p = ui.props();
    AutomationNode node;
    node.element = e;
    const auto kind = ui.tree().kind(e);
    node.type = kind ? automationType(*kind) : AutomationControlType::kGroup;
    node.name = nameOf(ui, e);
    node.enabled =
        !ui.tree().property(e, *p.IsEnabled).isBool() || ui.tree().property(e, *p.IsEnabled).asBool();
    node.focused = ui.focused() == e;
    node.bounds = ui.bounds(e);
    switch (node.type)
    {
    case AutomationControlType::kButton:
        node.focusable = true;
        break;
    case AutomationControlType::kCheckBox:
    case AutomationControlType::kRadioButton:
        node.focusable = true;
        node.toggled =
            ui.tree().property(e, *(kind == ElementKind::kToggleSwitch ? p.IsOn : p.IsChecked)).asBool();
        break;
    case AutomationControlType::kEdit:
        node.focusable = true;
        node.text = ui.tree().property(e, *p.Text).asString();
        break;
    case AutomationControlType::kSlider:
    case AutomationControlType::kProgressBar:
        node.focusable = node.type == AutomationControlType::kSlider;
        node.value = ui.tree().property(e, *p.Value).asDouble();
        node.minimum = ui.tree().property(e, *p.Minimum).asDouble();
        node.maximum = ui.tree().property(e, *p.Maximum).asDouble();
        break;
    default:
        break;
    }
    // A templated control is one node: its parts are presentation (UIA content view).
    if (!templated(ui, e))
        for (const ElementId child : ui.tree().children(e))
            if (ui.tree().property(child, *p.Visibility).asBool())
                node.children.push_back(build(ui, child));
    return node;
}

bool usable(Framework& ui, ElementId e, std::initializer_list<ElementKind> kinds)
{
    const auto kind = ui.tree().kind(e);
    if (!kind || std::ranges::find(kinds, *kind) == kinds.end())
        return false;
    const auto& enabled = ui.tree().property(e, *ui.props().IsEnabled);
    return !enabled.isBool() || enabled.asBool();
}
} // namespace

AutomationNode automationTree(Framework& ui, ElementId root)
{
    return build(ui, root);
}

bool automationInvoke(Framework& ui, ElementId e)
{
    if (!usable(ui, e, {ElementKind::kButton}))
        return false;
    ui.focus(e);
    ui.key(Key::kEnter); // the control's own keyboard activation: Click as for a user
    return true;
}

bool automationToggle(Framework& ui, ElementId e)
{
    if (!usable(ui, e, {ElementKind::kCheckBox, ElementKind::kRadioButton, ElementKind::kToggleSwitch}))
        return false;
    ui.focus(e);
    ui.key(Key::kEnter);
    return true;
}

bool automationSetRangeValue(Framework& ui, ElementId e, double value)
{
    if (!usable(ui, e, {ElementKind::kSlider}))
        return false;
    ui.set(e, ui.props().Value, PropertyValue(value)); // range coercion and ValueChanged
    return true;
}

bool automationSetValue(Framework& ui, ElementId e, std::string_view text)
{
    if (!usable(ui, e, {ElementKind::kTextBox}))
        return false;
    ui.focus(e);
    ui.set(e, ui.props().Text, PropertyValue(std::string(text)));
    return true;
}

} // namespace velora
