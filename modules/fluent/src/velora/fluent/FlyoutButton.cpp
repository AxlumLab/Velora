// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Derived from WinUI 2.3 DropDownButton.cpp, SplitButton.cpp and ToggleSplitButton.cpp.
#include "FluentTypes.hpp"
#include <stdexcept>
#include <utility>
#include <variant>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/DropDownButton.hpp>
#include <velora/fluent/ToggleSplitButton.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
const XamlTypeDeclaration& FlyoutButton::typeDeclaration(std::string_view name)
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    const auto* type = catalog.find(name);
    if (!type)
        throw std::runtime_error("missing Fluent button declaration");
    return *type;
}
const PropertyDescriptor& FlyoutButton::propertyFor(std::string_view type, std::string_view name)
{
    static PropertyRegistry registry;
    static const bool initialized = []
    {
        for (const auto* name :
             {"Velora.Fluent.DropDownButton", "Velora.Fluent.SplitButton", "Velora.Fluent.ToggleSplitButton",
              "Velora.Fluent.MenuBarItem", "Velora.Fluent.AppBarButton", "Velora.Fluent.AppBarToggleButton"})
            for (const auto& p : typeDeclaration(name).properties)
            {
                const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
                registry.define(p.runtimeName, std::get<PropertyValue>(parseValue(schema, p.defaultValue)),
                                PropertyFlags::kPaint);
            }
        return true;
    }();
    (void)initialized;
    return *registry.find(typeDeclaration(type).property(name)->runtimeName);
}
std::string FlyoutButton::registerControl(Framework& ui, const XamlTypeDeclaration& declaration,
                                          SourceControlRegistry::Factory factory)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : declaration.properties)
        properties.push_back(&propertyFor(declaration.className, p.name));
    return ui.registerSourceControl(declaration, std::move(factory), properties);
}
FlyoutButton::FlyoutButton(Variant variant) : UIElement(ElementKind::kUserControl), m_variant(variant)
{
    addPropertyChanged([this](DependencyObject&, const PropertyDescriptor&, const PropertyValue&,
                              const PropertyValue&) { updateParts(); });
}
std::string FlyoutButton::className() const
{
    if (m_variant == Variant::kAppBarButton)
        return "Velora.Fluent.AppBarButton";
    if (m_variant == Variant::kAppBarToggleButton)
        return "Velora.Fluent.AppBarToggleButton";
    if (m_variant == Variant::kMenuBarItem)
        return "Velora.Fluent.MenuBarItem";
    return m_variant == Variant::kDropDown ? "Velora.Fluent.DropDownButton"
           : m_variant == Variant::kSplit  ? "Velora.Fluent.SplitButton"
                                           : "Velora.Fluent.ToggleSplitButton";
}
std::string FlyoutButton::content() const
{
    return getValue(propertyFor(className(), contentPropertyName())).asString();
}
PropertySetResult FlyoutButton::setContent(std::string value)
{
    return setValue(propertyFor(className(), contentPropertyName()), std::move(value));
}
std::string_view FlyoutButton::contentPropertyName() const noexcept
{
    if (m_variant == Variant::kAppBarButton || m_variant == Variant::kAppBarToggleButton)
        return "Label";
    return m_variant == Variant::kMenuBarItem ? "Header" : "Content";
}
ElementId FlyoutButton::flyoutPassThrough() const noexcept
{
    return 0;
}
Framework* FlyoutButton::attachedFramework() const noexcept
{
    return m_ui;
}
std::shared_ptr<Flyout> FlyoutButton::flyout() const noexcept
{
    return m_flyout;
}
bool FlyoutButton::isFlyoutOpen() const noexcept
{
    return m_ui && m_flyout && m_flyout->isOpenAt(*m_ui, m_element);
}
void FlyoutButton::setFlyout(std::shared_ptr<Flyout> flyout)
{
    if (m_flyout == flyout)
        return;
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    closeFlyout();
    m_flyout = std::move(flyout);
    updateParts();
}
bool FlyoutButton::canInvoke() const
{
    if (!m_ui || !m_ui->isOwnerThread())
        return false;
    for (auto current = m_element; current; current = m_ui->tree().parent(current))
        if (!m_ui->tree().contains(current) ||
            !m_ui->tree().property(current, *m_ui->props().IsEnabled).asBool() ||
            !m_ui->tree().property(current, *m_ui->props().Visibility).asBool())
            return false;
    return true;
}
bool FlyoutButton::openFlyout()
{
    if (isFlyoutOpen())
        return true;
    if (!m_flyout || !canInvoke())
        return false;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto weak = std::weak_ptr<DependencyObject>(keepAlive);
    const auto flyout = m_flyout;
    m_ui->focus(m_element);
    if (!canInvoke())
        return false;
    return flyout->showAt(
        *m_ui, m_element,
        [weak](bool)
        {
            if (const auto object = weak.lock())
                static_cast<FlyoutButton&>(*object).updateParts();
        },
        flyoutPassThrough());
}
void FlyoutButton::closeFlyout()
{
    if (isFlyoutOpen())
    {
        const auto flyout = m_flyout;
        flyout->hide();
    }
}
void FlyoutButton::invokePrimary()
{
    if (!canInvoke())
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto command = m_command;
    if (!isFlyoutOpen())
        m_ui->focus(m_element);
    if (!canInvoke())
        return;
    if (m_variant == Variant::kMenuBarItem && isFlyoutOpen())
        closeFlyout();
    else if (m_variant == Variant::kDropDown || m_variant == Variant::kMenuBarItem ||
             (m_variant == Variant::kAppBarButton && m_flyout))
        openFlyout();
    else if (m_variant == Variant::kToggleSplit || m_variant == Variant::kAppBarToggleButton)
    {
        const auto& checked = propertyFor(className(), "IsChecked");
        setValue(checked, !getValue(checked).asBool());
    }
    if (m_ui && canInvoke())
        m_ui->tree().dispatch(m_element, m_ui->events().Click->id);
    if (command)
        command();
}
void FlyoutButton::setCommand(std::function<void()> command)
{
    if (!isOwnerThread())
        throw std::logic_error("Command belongs to the control's UI thread");
    m_command = std::move(command);
}
bool FlyoutButton::isInOverflow() const noexcept
{
    return false;
}
ElementId FlyoutButton::part(std::string_view name) const
{
    const auto found = m_parts.find(std::string(name));
    return found == m_parts.end() ? 0 : found->second;
}
bool FlyoutButton::validatePropertyValue(const PropertyDescriptor& d, const PropertyValue& v) const
{
    const auto& properties = typeDeclaration(className()).properties;
    for (const auto& property : properties)
    {
        if (property.id() != d.id)
            continue;
        if (property.kind == ValueKind::kBoolean)
            return v.isBool();
        if (!v.isString())
            return false;
        try
        {
            utf8ToUtf16(v.asString());
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return true;
}
void FlyoutButton::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    const bool appBar = m_variant == Variant::kAppBarButton || m_variant == Variant::kAppBarToggleButton;
    const auto uri =
        m_variant == Variant::kMenuBarItem
            ? "ms-appx-framework:///Controls/MenuBarItemView.vxbf"
            : (m_variant == Variant::kDropDown ? "ms-appx-framework:///Controls/DropDownButtonView.vxbf"
                                               : "ms-appx-framework:///Controls/SplitButtonView.vxbf");
    ComponentLoadOptions options;
    options.parent = this;
    m_overflowPresentation = appBar && isInOverflow();
    const auto loaded = loadComponent(
        ui,
        *ResourceUri::parse(appBar ? (m_overflowPresentation
                                          ? "ms-appx-framework:///Controls/AppBarOverflowButtonView.vxbf"
                                          : "ms-appx-framework:///Controls/AppBarButtonView.vxbf")
                                   : uri),
        options);
    if (!loaded.ok)
        throw std::runtime_error("Fluent button template: " + loaded.error);
    m_root = ui.tree().element(*loaded.root);
    std::vector<std::pair<std::string, ElementId>> names;
    for (const auto& [name, object] : loaded.names)
    {
        const auto id = ui.tree().element(*object);
        m_parts.emplace(name, id);
        names.emplace_back(name, id);
    }
    ui.applyTemplate(self, m_root, std::move(names));
    m_subscriptions.push_back(ui.subscribeEvent(part("PrimaryButton"), ui.events().Click,
                                                [this](ElementId, RoutedEventArgs& args)
                                                {
                                                    args.handled = true;
                                                    invokePrimary();
                                                }));
    if (part("SecondaryButton"))
        m_subscriptions.push_back(ui.subscribeEvent(part("SecondaryButton"), ui.events().Click,
                                                    [this](ElementId, RoutedEventArgs& args)
                                                    {
                                                        args.handled = true;
                                                        openFlyout();
                                                    }));
    updateParts();
}
void FlyoutButton::onDetached() noexcept
{
    m_subscriptions.clear();
    // Framework closes the popup on anchor removal, before this detach callback.
    m_ui = nullptr;
    m_element = m_root = 0;
    m_parts.clear();
}
void FlyoutButton::updateParts()
{
    if (!m_ui || !part("PrimaryButton"))
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    m_ui->set(part("PrimaryButton"), m_ui->props().Content, content());
    if (!m_ui)
        return;
    m_ui->goToState(m_root, isFlyoutOpen() ? "FlyoutOpen" : "FlyoutClosed");
    if (m_ui && (m_variant == Variant::kToggleSplit || m_variant == Variant::kAppBarToggleButton))
        m_ui->goToState(m_root,
                        getValue(propertyFor(className(), "IsChecked")).asBool() ? "Checked" : "Unchecked");
    if (m_ui && (m_variant == Variant::kAppBarButton || m_variant == Variant::kAppBarToggleButton))
    {
        const auto set =
            [this](const PropertyDescriptor* property, PropertyValue value, std::string_view name)
        {
            if (m_ui)
                if (const auto child = m_ui->templateChild(part("PrimaryButton"), name))
                    m_ui->set(child, property, std::move(value));
        };
        const auto& p = elementProperties().props;
        set(p.Text, getValue(propertyFor(className(), "IconGlyph")), "IconGlyph");
        set(p.Visibility, m_overflowPresentation || !getValue(propertyFor(className(), "IsCompact")).asBool(),
            "LabelText");
        if (m_ui)
            m_ui->set(part("PrimaryButton"), p.MinHeight,
                      m_overflowPresentation
                          ? 32.0
                          : (getValue(propertyFor(className(), "IsCompact")).asBool() ? 48.0 : 68.0));
    }
}
void FlyoutButton::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.key == Key::kF4 || (args.key == Key::kDown && args.alt))
        openFlyout();
    else if (args.key == Key::kEnter || args.key == Key::kSpace)
    {
        if (args.originalSource == part("SecondaryButton"))
            openFlyout();
        else
            invokePrimary();
    }
    else
        return;
    args.handled = true;
}
bool FlyoutButton::isFocusable() const noexcept
{
    return true;
}
std::string FlyoutButton::automationName() const
{
    return content();
}
std::optional<AutomationControlType> FlyoutButton::automationControlType() const noexcept
{
    return AutomationControlType::kButton;
}

DropDownButton::DropDownButton() : FlyoutButton(Variant::kDropDown)
{
}
SplitButton::SplitButton() : FlyoutButton(Variant::kSplit)
{
}
SplitButton::SplitButton(Variant variant) : FlyoutButton(variant)
{
}
ToggleSplitButton::ToggleSplitButton() : SplitButton(Variant::kToggleSplit)
{
}
#define VELORA_FLYOUT_BUTTON(type)                                                                           \
    const XamlTypeDeclaration& type::declaration()                                                           \
    {                                                                                                        \
        return typeDeclaration("Velora.Fluent." #type);                                                      \
    }                                                                                                        \
    const PropertyDescriptor& type::contentProperty()                                                        \
    {                                                                                                        \
        return propertyFor("Velora.Fluent." #type, "Content");                                               \
    }                                                                                                        \
    std::string type::registerType(Framework& ui)                                                            \
    {                                                                                                        \
        return registerControl(ui, declaration(), [] { return createElement<type>(); });                     \
    }
VELORA_FLYOUT_BUTTON(DropDownButton)
VELORA_FLYOUT_BUTTON(SplitButton)
VELORA_FLYOUT_BUTTON(ToggleSplitButton)
#undef VELORA_FLYOUT_BUTTON
const PropertyDescriptor& ToggleSplitButton::isCheckedProperty()
{
    return propertyFor("Velora.Fluent.ToggleSplitButton", "IsChecked");
}
bool ToggleSplitButton::isChecked() const
{
    return getValue(isCheckedProperty()).asBool();
}
PropertySetResult ToggleSplitButton::setIsChecked(bool value)
{
    return setValue(isCheckedProperty(), value);
}
u64 ToggleSplitButton::addIsCheckedChanged(std::function<void(bool)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [this, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                             const PropertyValue&, const PropertyValue& value)
        {
            if (isInTree() && p.id == isCheckedProperty().id)
                handler(value.asBool());
        });
}
} // namespace velora
