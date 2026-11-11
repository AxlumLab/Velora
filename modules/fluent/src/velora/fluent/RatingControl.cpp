// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 RatingControl.cpp/.idl/.xaml: value coercion and pointer/keyboard selection.
#include "FluentTypes.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <velora/controls/InputContext.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/RatingControl.hpp>
#include <velora/runtime/SchemaValue.hpp>
#include <velora/text/Encoding.hpp>

namespace velora
{
const XamlTypeDeclaration& RatingControl::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.RatingControl");
}
const PropertyDescriptor& RatingControl::property(std::string_view name)
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
std::string RatingControl::className() const
{
    return "Velora.Fluent.RatingControl";
}
std::string RatingControl::registerType(Framework& ui)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : declaration().properties)
        properties.push_back(&property(p.name));
    return ui.registerSourceControl(declaration(), [] { return createElement<RatingControl>(); }, properties);
}
RatingControl::RatingControl() : UIElement(ElementKind::kUserControl)
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            const auto& props = elementProperties().props;
            if (p.id == props.IsPointerOver->id && !getValue(*props.IsPointerOver).asBool())
                m_preview = -1;
            if (isReadOnly() || !getValue(*props.IsEnabled).asBool() || !getValue(*props.Visibility).asBool())
            {
                m_preview = -1;
                m_dragging = false;
                if (m_ui && m_ui->tree().pointerCapture() == m_element)
                    m_ui->tree().releasePointer();
            }
            if (p.name.starts_with("RatingControl.") || p.id == props.IsPointerOver->id ||
                p.id == props.IsEnabled->id)
                updateParts();
        });
}
const PropertyDescriptor& RatingControl::valueProperty()
{
    return property("Value");
}
const PropertyDescriptor& RatingControl::maxRatingProperty()
{
    return property("MaxRating");
}
const PropertyDescriptor& RatingControl::placeholderValueProperty()
{
    return property("PlaceholderValue");
}
f64 RatingControl::value() const
{
    return getValue(valueProperty()).asDouble();
}
PropertySetResult RatingControl::setValue(f64 v)
{
    return setValue(valueProperty(), v);
}
f64 RatingControl::placeholderValue() const
{
    return getValue(placeholderValueProperty()).asDouble();
}
PropertySetResult RatingControl::setPlaceholderValue(f64 v)
{
    return setValue(placeholderValueProperty(), v);
}
i32 RatingControl::maxRating() const
{
    return static_cast<i32>(getValue(maxRatingProperty()).asInt());
}
PropertySetResult RatingControl::setMaxRating(i32 v)
{
    return setValue(maxRatingProperty(), static_cast<i64>(v));
}
i32 RatingControl::initialSetValue() const
{
    return static_cast<i32>(getValue(property("InitialSetValue")).asInt());
}
PropertySetResult RatingControl::setInitialSetValue(i32 v)
{
    return setValue(property("InitialSetValue"), static_cast<i64>(v));
}
bool RatingControl::isReadOnly() const
{
    return getValue(property("IsReadOnly")).asBool();
}
PropertySetResult RatingControl::setIsReadOnly(bool v)
{
    return setValue(property("IsReadOnly"), v);
}
bool RatingControl::isClearEnabled() const
{
    return getValue(property("IsClearEnabled")).asBool();
}
PropertySetResult RatingControl::setIsClearEnabled(bool v)
{
    return setValue(property("IsClearEnabled"), v);
}
std::string RatingControl::caption() const
{
    return getValue(property("Caption")).asString();
}
PropertySetResult RatingControl::setCaption(std::string v)
{
    return setValue(property("Caption"), std::move(v));
}
bool RatingControl::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    for (const auto& d : declaration().properties)
    {
        if (d.id() != p.id)
            continue;
        if (d.kind == ValueKind::kDouble)
            return v.isDouble() && std::isfinite(v.asDouble());
        if (d.kind == ValueKind::kBoolean)
            return v.isBool();
        if (d.kind == ValueKind::kInteger)
            return v.isInt() && v.asInt() >= std::numeric_limits<i32>::min() &&
                   v.asInt() <= (p.id == maxRatingProperty().id ? 1000 : std::numeric_limits<i32>::max());
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
    }
    return true;
}
void RatingControl::normalizePropertyValues(const PropertyDescriptor& p)
{
    if (p.id != maxRatingProperty().id && p.id != valueProperty().id && p.id != placeholderValueProperty().id)
        return;
    if (maxRating() < 1)
        normalizeValue(maxRatingProperty(), i64{1});
    for (const auto* descriptor : {&valueProperty(), &placeholderValueProperty()})
    {
        const double v = getValue(*descriptor).asDouble();
        normalizeValue(*descriptor, v < 0 ? -1.0 : std::clamp(v, 1.0, static_cast<double>(maxRating())));
    }
}
void RatingControl::onAttached(Framework& ui, ElementId self)
{
    m_ui = &ui;
    m_element = self;
    ComponentLoadOptions options;
    options.parent = this;
    const auto loaded = loadComponent(
        ui, *ResourceUri::parse("ms-appx-framework:///Controls/RatingControlView.vxbf"), options);
    if (!loaded.ok)
        throw std::runtime_error("RatingControl template: " + loaded.error);
    m_background = ui.tree().element(*loaded.names.at("BackgroundStars"));
    m_foreground = ui.tree().element(*loaded.names.at("ForegroundStars"));
    m_clip = ui.tree().element(*loaded.names.at("ForegroundClip"));
    m_caption = ui.tree().element(*loaded.names.at("CaptionText"));
    ui.applyTemplate(self, ui.tree().element(*loaded.root), {});
    ui.set(m_clip, elementProperties().visual.clipToBounds, true);
    updateParts();
}
void RatingControl::onDetached() noexcept
{
    m_ui = nullptr;
    m_element = m_background = m_foreground = m_clip = m_caption = 0;
    m_starCount = 0;
    m_preview = -1;
    m_dragging = false;
}
void RatingControl::updateParts()
{
    if (!m_ui || !m_clip)
        return;
    const auto keepAlive = m_ui->tree().object(m_element);
    const auto& props = elementProperties().props;
    const auto set = [this](ElementId id, const PropertyDescriptor* p, PropertyValue v)
    {
        if (m_ui)
            m_ui->set(id, p, std::move(v));
    };
    if (m_starCount != maxRating())
    {
        for (const auto host : {m_background, m_foreground})
        {
            if (!m_ui)
                return;
            const auto old = m_ui->tree().children(host);
            for (const auto id : old)
                if (m_ui)
                    m_ui->tree().remove(id);
            for (i32 index = 0; m_ui && index < maxRating(); ++index)
            {
                const auto star = m_ui->create(ElementKind::kTextBlock, host);
                set(star, props.Text, host == m_background ? "\xEE\x9C\xB4" : "\xEE\x9C\xB5");
                set(star, props.FontFamily, "Segoe MDL2 Assets");
                set(star, props.FontSize, 24.0);
                set(star, props.Width, 32.0);
                set(star, props.Height, 32.0);
                set(star, props.Foreground, i64{0xFF767676});
            }
        }
        m_starCount = maxRating();
    }
    const bool placeholder = value() < 0;
    const double selected =
        m_preview >= 0 ? m_preview : (placeholder ? std::max(0.0, placeholderValue()) : value());
    set(m_foreground, props.Width, static_cast<double>(maxRating()) * 32);
    set(m_clip, props.Width, selected * 32);
    if (m_ui)
    {
        const auto stars = m_ui->tree().children(m_foreground);
        for (const auto star : stars)
            set(star, props.Foreground,
                (placeholder && m_preview < 0) || !getValue(*props.IsEnabled).asBool() ? i64{0xFF767676}
                                                                                       : i64{0xFF0078D4});
    }
    set(m_caption, props.Text, caption());
}
bool RatingControl::isFocusable() const noexcept
{
    return true;
}
std::string RatingControl::automationName() const
{
    return caption() + ": " + (value() < 0 ? "unrated" : std::to_string(value()));
}
f64 RatingControl::pointerRating(f32 x) const
{
    return std::clamp(std::ceil(static_cast<f64>(x) / 32), 0.0, static_cast<f64>(maxRating()));
}
void RatingControl::onPointerPressed(InputContext& context, PointerEventArgs& args)
{
    if (isReadOnly() || args.x < 0 || args.x > maxRating() * 32 || args.y < 0 || args.y > 32)
        return;
    context.focus();
    if (!m_ui || !context.capturePointer())
        return;
    m_dragging = true;
    m_preview = pointerRating(args.x);
    updateParts();
    args.handled = true;
}
void RatingControl::onPointerMoved(InputContext& context, PointerEventArgs& args)
{
    if (isReadOnly())
        return;
    if (m_dragging && !context.hasPointerCapture())
        m_dragging = false;
    m_preview =
        args.x >= 0 && args.x <= maxRating() * 32 && args.y >= 0 && args.y <= 32 ? pointerRating(args.x) : -1;
    updateParts();
}
void RatingControl::onPointerReleased(InputContext& context, PointerEventArgs& args)
{
    if (!m_dragging || !context.hasPointerCapture())
        return;
    m_dragging = false;
    context.releasePointer();
    m_preview = -1;
    if (!isReadOnly() && args.x >= 0 && args.x <= maxRating() * 32 && args.y >= 0 && args.y <= 32)
        userRate(pointerRating(args.x), true);
    updateParts();
    args.handled = true;
}
void RatingControl::userRate(f64 rating, bool mouse)
{
    if (!m_ui || isReadOnly() || !getValue(*elementProperties().props.IsEnabled).asBool())
        return;
    rating = std::clamp(rating, 0.0, static_cast<double>(maxRating()));
    if (value() < 0 && rating == 0)
        return;
    if (!isClearEnabled() && rating <= 0)
        rating = 1;
    else if ((rating == value() && isClearEnabled() && (mouse || rating != maxRating())) || rating == 0)
        rating = -1;
    setValue(rating);
    const auto handlers = m_handlers;
    const auto result = value();
    for (const auto& [token, handler] : handlers)
        if (m_ui && m_handlers.contains(token))
            handler(result);
}
void RatingControl::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (isReadOnly() || args.ctrl || args.alt)
        return;
    m_preview = -1;
    if (args.key == Key::kHome)
        userRate(0, false);
    else if (args.key == Key::kEnd)
        userRate(maxRating(), false);
    else if (args.key == Key::kLeft || args.key == Key::kDown || args.key == Key::kRight ||
             args.key == Key::kUp)
    {
        const bool increase = args.key == Key::kRight || args.key == Key::kUp;
        userRate(value() < 0
                     ? initialSetValue()
                     : (std::floor(value()) + (increase ? 1 : (std::floor(value()) == value() ? -1 : 0))),
                 false);
    }
    else
        return;
    args.handled = true;
    updateParts();
}
u64 RatingControl::addValueChanged(std::function<void(f64)> handler)
{
    if (!isOwnerThread())
        throw std::logic_error("RatingControl events belong to its UI thread");
    if (!handler)
        return 0;
    if (!m_nextHandler)
        throw std::overflow_error("RatingControl event tokens exhausted");
    const auto token = m_nextHandler++;
    m_handlers.emplace(token, std::move(handler));
    return token;
}
bool RatingControl::removeValueChanged(u64 token)
{
    return isOwnerThread() && m_handlers.erase(token) != 0;
}
} // namespace velora
