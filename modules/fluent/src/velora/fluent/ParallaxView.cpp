// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 ParallaxView.cpp/.idl: offsets, shift-ratio limits, sizing and clipping.
#include "FluentTypes.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/LayoutContext.hpp>
#include <velora/fluent/ParallaxView.hpp>
#include <velora/runtime/SchemaValue.hpp>
namespace velora
{
const XamlTypeDeclaration& ParallaxView::declaration()
{
    static const auto catalog = []
    {
        XamlTypeCatalog result;
        if (const auto error = result.append(kFluentTypes); !error.empty())
            throw std::runtime_error(error);
        return result;
    }();
    return *catalog.find("Velora.Fluent.ParallaxView");
}
const PropertyDescriptor& ParallaxView::property(std::string_view name)
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
ParallaxView::ParallaxView()
{
    addPropertyChanged(
        [this](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            if (p.name.starts_with("ParallaxView."))
                requestLayout();
        });
}
ParallaxView::~ParallaxView()
{
    if (!isOwnerThread())
        std::terminate();
    clearSourceBinding();
}
std::string ParallaxView::className() const
{
    return "Velora.Fluent.ParallaxView";
}
std::string ParallaxView::registerType(Framework& ui)
{
    std::vector<const PropertyDescriptor*> properties;
    for (const auto& p : declaration().properties)
        properties.push_back(&property(p.name));
    return ui.registerSourceControl(declaration(), [] { return createElement<ParallaxView>(); }, properties);
}
const PropertyDescriptor& ParallaxView::childProperty()
{
    static PropertyRegistry r;
    static const auto& p = r.define("ParallaxView.Child", ObjectRef{}, PropertyFlags::kPaint);
    return p;
}
const PropertyDescriptor& ParallaxView::hostedContentProperty() const
{
    return childProperty();
}
std::string_view ParallaxView::contentTemplateUri() const
{
    return "ms-appx-framework:///Controls/ParallaxViewView.vxbf";
}
std::shared_ptr<UIElement> ParallaxView::child() const
{
    return content();
}
PropertySetResult ParallaxView::setChild(std::shared_ptr<UIElement> value)
{
    return setContent(std::move(value));
}
#define VELORA_PARALLAX_ACCESSOR(Type, getter, Setter, name, accessor)                                       \
    Type ParallaxView::getter() const                                                                        \
    {                                                                                                        \
        return getValue(property(name)).accessor();                                                          \
    }                                                                                                        \
    PropertySetResult ParallaxView::set##Setter(Type value)                                                  \
    {                                                                                                        \
        return setValue(property(name), std::move(value));                                                   \
    }
VELORA_PARALLAX_ACCESSOR(f64, horizontalShift, HorizontalShift, "HorizontalShift", asDouble)
VELORA_PARALLAX_ACCESSOR(f64, verticalShift, VerticalShift, "VerticalShift", asDouble)
VELORA_PARALLAX_ACCESSOR(f64, horizontalSourceStartOffset, HorizontalSourceStartOffset,
                         "HorizontalSourceStartOffset", asDouble)
VELORA_PARALLAX_ACCESSOR(f64, horizontalSourceEndOffset, HorizontalSourceEndOffset,
                         "HorizontalSourceEndOffset", asDouble)
VELORA_PARALLAX_ACCESSOR(f64, verticalSourceStartOffset, VerticalSourceStartOffset,
                         "VerticalSourceStartOffset", asDouble)
VELORA_PARALLAX_ACCESSOR(f64, verticalSourceEndOffset, VerticalSourceEndOffset, "VerticalSourceEndOffset",
                         asDouble)
VELORA_PARALLAX_ACCESSOR(f64, maxHorizontalShiftRatio, MaxHorizontalShiftRatio, "MaxHorizontalShiftRatio",
                         asDouble)
VELORA_PARALLAX_ACCESSOR(f64, maxVerticalShiftRatio, MaxVerticalShiftRatio, "MaxVerticalShiftRatio", asDouble)
VELORA_PARALLAX_ACCESSOR(std::string, horizontalSourceOffsetKind, HorizontalSourceOffsetKind,
                         "HorizontalSourceOffsetKind", asString)
VELORA_PARALLAX_ACCESSOR(std::string, verticalSourceOffsetKind, VerticalSourceOffsetKind,
                         "VerticalSourceOffsetKind", asString)
VELORA_PARALLAX_ACCESSOR(bool, isHorizontalShiftClamped, IsHorizontalShiftClamped, "IsHorizontalShiftClamped",
                         asBool)
VELORA_PARALLAX_ACCESSOR(bool, isVerticalShiftClamped, IsVerticalShiftClamped, "IsVerticalShiftClamped",
                         asBool)
#undef VELORA_PARALLAX_ACCESSOR
bool ParallaxView::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == childProperty().id && !value.isObject())
        return false;
    for (const auto& d : declaration().properties)
    {
        if (d.id() != p.id)
            continue;
        if (d.kind == ValueKind::kBoolean)
            return value.isBool();
        if (d.kind == ValueKind::kString)
            return value.isString() && (value.asString() == "Relative" || value.asString() == "Absolute");
        return value.isDouble() && std::isfinite(value.asDouble()) &&
               std::abs(value.asDouble()) <= std::numeric_limits<f32>::max() &&
               (!d.name.starts_with("Max") || value.asDouble() >= 0);
    }
    return ContentElement::validatePropertyValue(p, value);
}
std::shared_ptr<UIElement> ParallaxView::source() const
{
    return m_source.lock();
}
void ParallaxView::setSource(std::shared_ptr<UIElement> value)
{
    if (!isOwnerThread() || (value && (!value->isOwnerThread() || !value->is(ElementKind::kScrollViewer))))
        throw std::invalid_argument("Parallax source must be a UI-thread ScrollViewer");
    if (auto* ui = attachedFramework(); ui && value && !ui->tree().element(*value))
        throw std::invalid_argument("Parallax source must be in the same UI tree");
    clearSourceBinding();
    m_source = value;
    bindSource();
    requestLayout();
}
void ParallaxView::clearSourceBinding()
{
    m_viewChanged.reset();
    if (const auto source = m_source.lock(); source && m_sourceToken)
        source->removePropertyChanged(m_sourceToken);
    m_sourceToken = 0;
}
void ParallaxView::bindSource()
{
    clearSourceBinding();
    auto* ui = attachedFramework();
    const auto source = m_source.lock();
    if (!ui || !source)
        return;
    const auto id = ui->tree().element(*source);
    if (!id)
        throw std::invalid_argument("Parallax source is not in this UI tree");
    const auto weak = std::weak_ptr<DependencyObject>(ui->tree().object(ownerElement()));
    m_sourceToken = source->addPropertyChanged(
        [weak](DependencyObject&, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue&)
        {
            const auto& props = elementProperties().props;
            if (p.id != props.HorizontalOffset->id && p.id != props.VerticalOffset->id &&
                p.id != props.ExtentWidth->id && p.id != props.ExtentHeight->id &&
                p.id != props.ViewportWidth->id && p.id != props.ViewportHeight->id)
                return;
            if (const auto owner = weak.lock())
                static_cast<ParallaxView&>(*owner).requestLayout();
        });
    m_viewChanged = ui->subscribeEvent(id, ui->events().ViewChanged,
                                       [weak](ElementId, RoutedEventArgs&)
                                       {
                                           if (const auto owner = weak.lock())
                                               static_cast<ParallaxView&>(*owner).requestLayout();
                                       });
    const std::weak_ptr<UIElement> weakSource = source;
    ui->tree().onRemoved(id,
                         [weak, weakSource]
                         {
                             if (const auto owner = weak.lock())
                             {
                                 auto& view = static_cast<ParallaxView&>(*owner);
                                 if (view.m_source.lock() == weakSource.lock())
                                 {
                                     view.clearSourceBinding();
                                     view.requestLayout();
                                 }
                             }
                         });
}
void ParallaxView::requestLayout()
{
    if (auto* ui = attachedFramework())
        ui->tree().invalidateMeasure(ownerElement());
}
void ParallaxView::refreshAutomaticHorizontalOffsets()
{
    if (!isOwnerThread())
        throw std::logic_error("Parallax offsets belong to the UI thread");
    requestLayout();
}
void ParallaxView::refreshAutomaticVerticalOffsets()
{
    if (!isOwnerThread())
        throw std::logic_error("Parallax offsets belong to the UI thread");
    requestLayout();
}
f64 ParallaxView::horizontalTranslation() const noexcept
{
    return m_horizontalTranslation;
}
f64 ParallaxView::verticalTranslation() const noexcept
{
    return m_verticalTranslation;
}
void ParallaxView::onAttached(Framework& ui, ElementId self)
{
    ContentElement::onAttached(ui, self);
    ui.set(self, elementProperties().visual.clipToBounds, true);
    bindSource();
}
void ParallaxView::onDetached() noexcept
{
    clearSourceBinding();
    m_horizontalTranslation = m_verticalTranslation = 0;
    ContentElement::onDetached();
}
f64 ParallaxView::axisTranslation(bool horizontal, Size viewport) const
{
    auto* ui = attachedFramework();
    const auto source = m_source.lock();
    if (!ui || !source)
        return 0;
    const auto id = ui->tree().element(*source);
    if (!id)
        return 0;
    const auto& props = elementProperties().props;
    const auto value = [&](const PropertyDescriptor* p) { return source->getValue(*p).asDouble(); };
    const auto offset = value(horizontal ? props.HorizontalOffset : props.VerticalOffset);
    const auto extent = value(horizontal ? props.ExtentWidth : props.ExtentHeight);
    const auto sourceViewport = value(horizontal ? props.ViewportWidth : props.ViewportHeight);
    const auto shift = horizontal ? horizontalShift() : verticalShift();
    if (shift == 0)
        return 0;
    const auto maxRatio = horizontal ? maxHorizontalShiftRatio() : maxVerticalShiftRatio();
    double start = horizontal ? horizontalSourceStartOffset() : verticalSourceStartOffset();
    double end = horizontal ? horizontalSourceEndOffset() : verticalSourceEndOffset();
    const auto kind = horizontal ? horizontalSourceOffsetKind() : verticalSourceOffsetKind();
    if (kind == "Relative")
    {
        if (ui->isWithin(ownerElement(), id))
        {
            const auto target = ui->bounds(ownerElement()), sourceBounds = ui->bounds(id);
            const auto position =
                (horizontal ? target.x - sourceBounds.x : target.y - sourceBounds.y) + offset;
            start += position - sourceViewport;
            end += position + (horizontal ? viewport.width : viewport.height);
        }
        else
            end = std::max(0.0, extent + end - sourceViewport);
    }
    else
        end = std::max(0.0, end);
    const auto range = end - start;
    const bool clamped = horizontal ? isHorizontalShiftClamped() : isVerticalShiftClamped();
    if (clamped)
    {
        const auto amount = std::min(maxRatio * std::max(0.0, range), std::abs(shift));
        if (offset <= start)
            return shift > 0 ? 0 : -amount;
        if (offset >= end)
            return shift > 0 ? -amount : 0;
    }
    if (range == 0)
        return 0;
    const auto ratio = std::min(maxRatio, std::abs(shift) / range);
    return shift > 0 ? -ratio * (offset - start) : ratio * (offset - end);
}
Size ParallaxView::measureOverride(LayoutContext& context, ElementId, const MeasureInput& input)
{
    Size desired;
    if (const auto node = contentNode())
        desired = context.measure(node, {input.content.width + static_cast<f32>(std::abs(horizontalShift())),
                                         input.content.height + static_cast<f32>(std::abs(verticalShift()))});
    return {std::isfinite(input.content.width) ? input.available.width : desired.width + input.insetX,
            std::isfinite(input.content.height) ? input.available.height : desired.height + input.insetY};
}
void ParallaxView::arrangeOverride(LayoutContext& context, ElementId, Box box, const std::vector<ElementId>&)
{
    m_horizontalTranslation = m_verticalTranslation = 0;
    if (!attachedFramework() || !contentHost())
        return;
    context.arrange(contentHost(), box);
    const auto node = contentNode();
    if (!node || !attachedFramework())
        return;
    auto desired = context.desired(node);
    const auto& props = context.props();
    const bool stretchX = context.property(node, props.Width).empty() &&
                          context.text(node, props.HorizontalAlignment) == "Stretch";
    const bool stretchY = context.property(node, props.Height).empty() &&
                          context.text(node, props.VerticalAlignment) == "Stretch";
    const auto minWidth = box.width + static_cast<f32>(std::abs(horizontalShift()));
    const auto minHeight = box.height + static_cast<f32>(std::abs(verticalShift()));
    if (horizontalShift() != 0 && desired.width < minWidth)
    {
        const auto ratio = desired.width > 0 ? minWidth / desired.width : 0;
        desired.width = minWidth;
        if (ratio && stretchY)
            desired.height *= ratio;
    }
    if (verticalShift() != 0 && desired.height < minHeight)
    {
        const auto ratio = desired.height > 0 ? minHeight / desired.height : 0;
        desired.height = minHeight;
        if (ratio && stretchX)
            desired.width *= ratio;
    }
    if (stretchX)
        desired.width = std::max(desired.width, box.width);
    if (stretchY)
        desired.height = std::max(desired.height, box.height);
    m_horizontalTranslation = axisTranslation(true, {box.width, box.height});
    m_verticalTranslation = axisTranslation(false, {box.width, box.height});
    const auto alignX = context.text(node, props.HorizontalAlignment),
               alignY = context.text(node, props.VerticalAlignment);
    const auto x = alignX == "Center"  ? (box.width - desired.width) / 2
                   : alignX == "Right" ? box.width - desired.width
                                       : 0;
    const auto y = alignY == "Center"   ? (box.height - desired.height) / 2
                   : alignY == "Bottom" ? box.height - desired.height
                                        : 0;
    context.arrange(node, {x + static_cast<f32>(m_horizontalTranslation),
                           y + static_cast<f32>(m_verticalTranslation), desired.width, desired.height});
}
} // namespace velora
