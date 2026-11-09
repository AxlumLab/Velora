// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Derived from dev/PersonPicture/PersonPicture.cpp at WinUI 2.3; Windows contact access and
// WinRT projections are replaced by ordinary Velora properties and compiled Fluent visuals.
#include "FluentTypes.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/LayoutContext.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/InitialsGenerator.hpp>
#include <velora/fluent/PersonPicture.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
const XamlTypeDeclaration& PersonPicture::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog catalog;
        if (const auto error = catalog.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return catalog;
    }();
    const auto* type = catalog.find("Velora.Fluent.PersonPicture");
    if (!type)
        throw std::runtime_error("missing internal PersonPicture declaration");
    return *type;
}

const PropertyDescriptor& PersonPicture::property(std::string_view name)
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
    const auto* declared = declaration().property(name);
    if (!declared)
        throw std::runtime_error("unknown PersonPicture property");
    return *registry.find(declared->runtimeName);
}

PersonPicture::PersonPicture() : UIElement(ElementKind::kUserControl)
{
    const auto& p = elementProperties().props;
    setValue(*p.Width, 100.0, PropertySource::kDefaultStyle);
    setValue(*p.Height, 100.0, PropertySource::kDefaultStyle);
    setValue(*p.IsTabStop, false, PropertySource::kDefaultStyle);
    addPropertyChanged([this](DependencyObject&, const PropertyDescriptor&, const PropertyValue&,
                              const PropertyValue&) { updateParts(); });
}

std::string PersonPicture::className() const
{
    return declaration().className;
}

std::string PersonPicture::registerType(Framework& ui)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : declaration().properties)
        properties.push_back(&property(p.name));
    return ui.registerSourceControl(declaration(), [] { return createElement<PersonPicture>(); }, properties);
}

#define VELORA_PERSON_STRING(method, setter, name)                                                           \
    const PropertyDescriptor& PersonPicture::method##Property()                                              \
    {                                                                                                        \
        return property(#name);                                                                              \
    }                                                                                                        \
    std::string PersonPicture::method() const                                                                \
    {                                                                                                        \
        return getValue(method##Property()).asString();                                                      \
    }                                                                                                        \
    PropertySetResult PersonPicture::setter(std::string value)                                               \
    {                                                                                                        \
        return setValue(method##Property(), PropertyValue(std::move(value)));                                \
    }
VELORA_PERSON_STRING(displayName, setDisplayName, DisplayName)
VELORA_PERSON_STRING(initials, setInitials, Initials)
VELORA_PERSON_STRING(profilePicture, setProfilePicture, ProfilePicture)
VELORA_PERSON_STRING(badgeGlyph, setBadgeGlyph, BadgeGlyph)
VELORA_PERSON_STRING(badgeImageSource, setBadgeImageSource, BadgeImageSource)
VELORA_PERSON_STRING(badgeText, setBadgeText, BadgeText)
#undef VELORA_PERSON_STRING

const PropertyDescriptor& PersonPicture::isGroupProperty()
{
    return property("IsGroup");
}
bool PersonPicture::isGroup() const
{
    return getValue(isGroupProperty()).asBool();
}
PropertySetResult PersonPicture::setIsGroup(bool value)
{
    return setValue(isGroupProperty(), value);
}
const PropertyDescriptor& PersonPicture::badgeNumberProperty()
{
    return property("BadgeNumber");
}
i32 PersonPicture::badgeNumber() const
{
    return static_cast<i32>(getValue(badgeNumberProperty()).asInt());
}
PropertySetResult PersonPicture::setBadgeNumber(i32 value)
{
    return setValue(badgeNumberProperty(), i64{value});
}

bool PersonPicture::validatePropertyValue(const PropertyDescriptor& d, const PropertyValue& value) const
{
    for (const auto& p : declaration().properties)
    {
        if (p.id() != d.id)
            continue;
        if (p.kind == ValueKind::kBoolean)
            return value.isBool();
        if (p.kind == ValueKind::kInteger)
            return value.isInt() && value.asInt() >= std::numeric_limits<i32>::min() &&
                   value.asInt() <= std::numeric_limits<i32>::max();
        if (!value.isString())
            return false;
        try
        {
            utf8ToUtf16(value.asString());
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
    return true;
}

std::string PersonPicture::actualInitials() const
{
    const auto explicitInitials = initials();
    return explicitInitials.empty() ? InitialsGenerator::fromDisplayName(displayName()) : explicitInitials;
}

std::string PersonPicture::visualState() const
{
    if (isGroup())
        return "Group";
    if (!profilePicture().empty())
        return "Photo";
    return actualInitials().empty() ? "NoPhotoOrInitials" : "Initials";
}

std::string PersonPicture::badgeState() const
{
    if (!badgeImageSource().empty())
        return "BadgeWithImageSource";
    if (badgeNumber() != 0)
        return badgeNumber() > 0 ? "BadgeWithoutImageSource" : "NoBadge";
    return badgeGlyph().empty() ? "NoBadge" : "BadgeWithoutImageSource";
}

std::string PersonPicture::badgeLabel() const
{
    if (!badgeImageSource().empty())
        return {};
    const auto number = badgeNumber();
    if (number < 0)
        return {};
    if (number > 99)
        return "99+";
    return number ? std::to_string(number) : badgeGlyph();
}

std::string PersonPicture::automationName() const
{
    std::string name = isGroup() ? "Group" : displayName();
    if (name.empty())
        name = initials();
    if (name.empty())
        name = "Person";
    const auto number = badgeNumber();
    if (number > 0)
        return name + ", " + std::to_string(number) + " " +
               (badgeText().empty() ? (number == 1 ? "item" : "items") : badgeText());
    if (!badgeGlyph().empty() || !badgeImageSource().empty())
        return name + ", " + (badgeText().empty() ? "icon" : badgeText());
    return name;
}

std::optional<AutomationControlType> PersonPicture::automationControlType() const noexcept
{
    return AutomationControlType::kText;
}

void PersonPicture::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/PersonPictureView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("PersonPicture template: " + loaded.error);
    m_root = ui.tree().element(*loaded.root);
    std::vector<std::pair<std::string, ElementId>> names;
    for (const auto& [name, object] : loaded.names)
    {
        const auto id = ui.tree().element(*object);
        m_parts.emplace(name, id);
        names.emplace_back(name, id);
    }
    ui.applyTemplate(self, m_root, std::move(names));
    setPart("AvatarCircle", elementProperties().visual.clipToBounds, true);
    setPart("BadgeCircle", elementProperties().visual.clipToBounds, true);
    resizeParts(100);
    updateParts();
}

void PersonPicture::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_root = 0;
    m_parts.clear();
}

void PersonPicture::setPart(std::string_view name, const PropertyDescriptor* property, PropertyValue value)
{
    if (!m_ui || !m_ui->tree().contains(m_element))
        return;
    if (const auto found = m_parts.find(std::string(name)); found != m_parts.end())
        m_ui->set(found->second, property, std::move(value));
}

void PersonPicture::updateParts()
{
    if (!m_ui || !m_ui->tree().contains(m_root))
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto& p = elementProperties().props;
    setPart("InitialsTextBlock", p.Text, actualInitials());
    setPart("ProfileImage", p.Source, profilePicture());
    setPart("BadgeImage", p.Source, badgeImageSource());
    setPart("BadgeNumberTextBlock", p.Text, badgeLabel());
    setPart("BadgeNumberTextBlock", p.FontFamily,
            badgeNumber() == 0 && !badgeGlyph().empty() ? "Segoe MDL2 Assets" : "Segoe UI");
    if (m_ui && m_ui->tree().contains(m_root))
        m_ui->goToState(m_root, visualState());
    if (m_ui && m_ui->tree().contains(m_root))
        m_ui->goToState(m_root, badgeState());
}

void PersonPicture::resizeParts(f32 diameter)
{
    const auto& p = elementProperties().props;
    setPart("AvatarCircle", p.CornerRadius, static_cast<f64>(diameter / 2));
    setPart("InitialsTextBlock", p.FontSize, std::max(1.0, diameter * .42));
    setPart("BadgeGrid", p.Width, static_cast<f64>(diameter * .5f));
    setPart("BadgeGrid", p.Height, static_cast<f64>(diameter * .5f));
    setPart("BadgeCircle", p.CornerRadius, static_cast<f64>(diameter / 4));
    setPart("BadgeNumberTextBlock", p.FontSize, std::max(1.0, diameter * .3));
    setPart("SingleIcon", p.FontSize, std::max(1.0, diameter * .42));
    setPart("GroupIcon", p.FontSize, std::max(1.0, diameter * .42));
}

Size PersonPicture::measureOverride(LayoutContext& context, ElementId, const MeasureInput& input)
{
    for (const auto child : input.children)
        context.measure(child, input.content);
    return {100 + input.insetX, 100 + input.insetY};
}

void PersonPicture::arrangeOverride(LayoutContext& context, ElementId, Box content,
                                    const std::vector<ElementId>& children)
{
    const f32 diameter = std::max(0.0f, std::min(content.width, content.height));
    resizeParts(diameter);
    for (const auto child : children)
    {
        context.measure(child, {diameter, diameter});
        context.arrange(child, {content.x + (content.width - diameter) / 2,
                                content.y + (content.height - diameter) / 2, diameter, diameter});
    }
}
} // namespace velora
