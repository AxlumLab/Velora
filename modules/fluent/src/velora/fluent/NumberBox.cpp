// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Derived from WinUI 2.3 NumberBox.cpp/.idl; one actual Velora object and property store.
#include "FluentTypes.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/NumberBox.hpp>
#include <velora/fluent/NumberBoxParser.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
const XamlTypeDeclaration& NumberBox::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.NumberBox");
}
const PropertyDescriptor& NumberBox::property(std::string_view name)
{
    static PropertyRegistry registry;
    static const bool initialized = []
    {
        for (const auto& p : declaration().properties)
        {
            const SchemaProperty schema{p.name, p.kind, {}, p.defaultValue, kSchemaPaint};
            registry.define(p.runtimeName, std::get<PropertyValue>(parseValue(schema, p.defaultValue)),
                            PropertyFlags::kPaint);
        }
        return true;
    }();
    (void)initialized;
    return *registry.find(declaration().property(name)->runtimeName);
}
NumberBox::NumberBox() : UIElement(ElementKind::kUserControl)
{
    setValue(textProperty(), "0", PropertySource::kDefaultStyle);
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        { updateParts(p.id == textProperty().id || p.id == valueProperty().id); });
}
std::string NumberBox::className() const
{
    return declaration().className;
}
std::string NumberBox::registerType(Framework& ui)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : declaration().properties)
        properties.push_back(&property(p.name));
    return ui.registerSourceControl(declaration(), [] { return createElement<NumberBox>(); }, properties);
}
#define VELORA_NUMBER_PROPERTY(type, method, setter, name, conversion)                                       \
    const PropertyDescriptor& NumberBox::method##Property()                                                  \
    {                                                                                                        \
        return property(name);                                                                               \
    }                                                                                                        \
    type NumberBox::method() const                                                                           \
    {                                                                                                        \
        return getValue(method##Property()).conversion();                                                    \
    }                                                                                                        \
    PropertySetResult NumberBox::setter(type value)                                                          \
    {                                                                                                        \
        return setValue(method##Property(), PropertyValue(std::move(value)));                                \
    }
VELORA_NUMBER_PROPERTY(f64, value, setValue, "Value", asDouble)
VELORA_NUMBER_PROPERTY(f64, minimum, setMinimum, "Minimum", asDouble)
VELORA_NUMBER_PROPERTY(f64, maximum, setMaximum, "Maximum", asDouble)
VELORA_NUMBER_PROPERTY(f64, smallChange, setSmallChange, "SmallChange", asDouble)
VELORA_NUMBER_PROPERTY(f64, largeChange, setLargeChange, "LargeChange", asDouble)
VELORA_NUMBER_PROPERTY(bool, isWrapEnabled, setIsWrapEnabled, "IsWrapEnabled", asBool)
VELORA_NUMBER_PROPERTY(bool, acceptsExpression, setAcceptsExpression, "AcceptsExpression", asBool)
VELORA_NUMBER_PROPERTY(std::string, text, setText, "Text", asString)
VELORA_NUMBER_PROPERTY(std::string, header, setHeader, "Header", asString)
VELORA_NUMBER_PROPERTY(std::string, placeholderText, setPlaceholderText, "PlaceholderText", asString)
VELORA_NUMBER_PROPERTY(std::string, validationMode, setValidationMode, "ValidationMode", asString)
VELORA_NUMBER_PROPERTY(std::string, spinButtonPlacementMode, setSpinButtonPlacementMode,
                       "SpinButtonPlacementMode", asString)
#undef VELORA_NUMBER_PROPERTY

std::string NumberBox::format(f64 value)
{
    if (std::isnan(value))
        return {};
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 12);
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
}
bool NumberBox::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    for (const auto& d : declaration().properties)
    {
        if (d.id() != p.id)
            continue;
        if (d.kind == ValueKind::kDouble)
            return v.isDouble() &&
                   (std::isfinite(v.asDouble()) || (p.id == valueProperty().id && std::isnan(v.asDouble())));
        if (d.kind == ValueKind::kBoolean)
            return v.isBool();
        if (!v.isString())
            return false;
        try
        {
            utf8ToUtf16(v.asString());
        }
        catch (const std::exception&)
        {
            return false;
        }
        if (p.id == validationModeProperty().id)
            return v.asString() == "InvalidInputOverwritten" || v.asString() == "Disabled";
        if (p.id == spinButtonPlacementModeProperty().id)
            return v.asString() == "Hidden" || v.asString() == "Inline";
    }
    return true;
}
void NumberBox::normalizePropertyValues(const PropertyDescriptor& p)
{
    const bool numeric = p.id == valueProperty().id || p.id == minimumProperty().id ||
                         p.id == maximumProperty().id || p.id == validationModeProperty().id;
    if (!numeric && p.id != textProperty().id)
        return;
    if (minimum() > maximum())
    {
        if (p.id == maximumProperty().id)
            normalizeValue(minimumProperty(), maximum());
        else
            normalizeValue(maximumProperty(), minimum());
    }
    if (p.id == textProperty().id)
    {
        const auto input = text();
        const bool empty = input.find_first_not_of(" \t\n\r\f\v") == std::string::npos;
        const auto parsed = NumberBoxParser::compute(input, acceptsExpression());
        if (empty)
        {
            if (!std::isnan(value()))
                normalizeValue(valueProperty(), std::numeric_limits<f64>::quiet_NaN());
        }
        else if (parsed)
            normalizeValue(valueProperty(), *parsed);
        else if (validationMode() == "Disabled")
            return;
    }
    if (validationMode() != "Disabled" && !std::isnan(value()))
        normalizeValue(valueProperty(), std::clamp(value(), minimum(), maximum()));
    normalizeValue(textProperty(), format(value()));
}
u64 NumberBox::addValueChanged(std::function<void(f64, f64)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                       const PropertyValue& before, const PropertyValue& after)
        {
            if (p.id == valueProperty().id &&
                !(std::isnan(before.asDouble()) && std::isnan(after.asDouble())))
                handler(before.asDouble(), after.asDouble());
        });
}
ElementId NumberBox::part(std::string_view name) const
{
    const auto found = m_parts.find(std::string(name));
    return found == m_parts.end() ? 0 : found->second;
}
void NumberBox::commitInput()
{
    if (!m_ui || !part("InputBox"))
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    setText(m_ui->tree().property(part("InputBox"), *m_ui->props().Text).asString());
    // Identical public Text may leave a draft needing restoration after invalid input.
    updateParts(true);
}
void NumberBox::cancelInput()
{
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    // Restore the display without reparsing its rounded spelling back into Value.
    normalizeValue(textProperty(), format(value()));
    updateParts(true);
}
void NumberBox::stepValue(f64 change)
{
    if (!std::isfinite(change))
        return;
    const auto keepAlive = m_ui ? m_ui->tree().object(m_element) : nullptr;
    const bool attached = m_ui != nullptr;
    commitInput();
    if ((attached && !m_ui) || std::isnan(value()))
        return;
    const f64 next = value() + change;
    if (isWrapEnabled() && next > maximum())
        setValue(minimum());
    else if (isWrapEnabled() && next < minimum())
        setValue(maximum());
    else if (std::isfinite(next))
        setValue(next);
    else if (validationMode() != "Disabled")
        setValue(change > 0 ? maximum() : minimum());
}
void NumberBox::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.originalSource != part("InputBox"))
        return;
    switch (args.key)
    {
    case Key::kUp:
        stepValue(smallChange());
        break;
    case Key::kDown:
        stepValue(-smallChange());
        break;
    case Key::kPageUp:
        stepValue(largeChange());
        break;
    case Key::kPageDown:
        stepValue(-largeChange());
        break;
    case Key::kEnter:
        commitInput();
        break;
    case Key::kEscape:
        cancelInput();
        break;
    default:
        return;
    }
    args.handled = true;
}
void NumberBox::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded =
        loadComponent(ui, *ResourceUri::parse("ms-appx-framework:///Controls/NumberBoxView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("NumberBox template: " + loaded.error);
    std::vector<std::pair<std::string, ElementId>> names;
    for (const auto& [name, object] : loaded.names)
    {
        const auto id = ui.tree().element(*object);
        m_parts.emplace(name, id);
        names.emplace_back(name, id);
    }
    ui.applyTemplate(self, ui.tree().element(*loaded.root), std::move(names));
    m_subscriptions.push_back(ui.subscribeEvent(part("UpSpinButton"), ui.events().Click,
                                                [this](ElementId, RoutedEventArgs&)
                                                { stepValue(smallChange()); }));
    m_subscriptions.push_back(ui.subscribeEvent(part("DownSpinButton"), ui.events().Click,
                                                [this](ElementId, RoutedEventArgs&)
                                                { stepValue(-smallChange()); }));
    m_subscriptions.push_back(ui.subscribeEvent(part("InputBox"), ui.events().LostFocus,
                                                [this](ElementId, RoutedEventArgs&) { commitInput(); }));
    m_subscriptions.push_back(ui.subscribeEvent(
        part("InputBox"), ui.events().GotFocus,
        [this](ElementId, RoutedEventArgs&)
        {
            if (!m_ui)
                return;
            const auto keepAlive = m_ui->tree().object(m_element);
            const auto length =
                m_ui->tree().property(part("InputBox"), *m_ui->props().Text).asString().size();
            m_ui->set(part("InputBox"), m_ui->props().SelectionAnchor, i64{0});
            if (m_ui)
                m_ui->set(part("InputBox"), m_ui->props().CaretPosition, static_cast<i64>(length));
        }));
    updateParts(true);
}
void NumberBox::onDetached() noexcept
{
    m_subscriptions.clear();
    m_ui = nullptr;
    m_element = 0;
    m_parts.clear();
}
void NumberBox::updateParts(bool updateText)
{
    if (!m_ui)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto set = [this](std::string_view name, const PropertyDescriptor* p, PropertyValue v)
    {
        if (m_ui && part(name))
            m_ui->set(part(name), p, std::move(v));
    };
    const auto& p = elementProperties().props;
    if (updateText)
    {
        set("InputBox", p.Text, text());
        set("InputBox", p.CaretPosition, static_cast<i64>(text().size()));
        set("InputBox", p.SelectionAnchor, static_cast<i64>(text().size()));
    }
    set("InputBox", p.PlaceholderText, placeholderText());
    set("HeaderContentPresenter", p.Text, header());
    set("HeaderContentPresenter", p.Visibility, !header().empty());
    set("SpinButtons", p.Visibility, spinButtonPlacementMode() == "Inline");
    const bool canSpin = !std::isnan(value());
    const bool unbounded = isWrapEnabled() || validationMode() == "Disabled";
    set("UpSpinButton", p.IsEnabled,
        canSpin && (unbounded || (smallChange() >= 0 ? value() < maximum() : value() > minimum())));
    set("DownSpinButton", p.IsEnabled,
        canSpin && (unbounded || (smallChange() >= 0 ? value() > minimum() : value() < maximum())));
}
std::string NumberBox::automationName() const
{
    return header();
}
std::optional<AutomationControlType> NumberBox::automationControlType() const noexcept
{
    return AutomationControlType::kEdit;
}
} // namespace velora
