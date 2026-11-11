// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// Adapted from WinUI 2.3 TwoPaneView.cpp/.idl/.xaml: single-display mode selection and pane tracks.
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/TwoPaneView.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/runtime/SchemaValue.hpp>

namespace velora
{
namespace
{
const ReadOnlyPropertyKey& modeKey()
{
    static PropertyRegistry registry;
    static const auto key = []
    {
        PropertyDescriptor property;
        property.name = "TwoPaneView.Mode";
        property.defaultValue = "SinglePane";
        property.flags = PropertyFlags::kPaint;
        return registry.defineReadOnly(std::move(property));
    }();
    return key;
}
std::optional<GridLength> parseLength(const PropertyValue& value)
{
    if (!value.isString())
        return {};
    const SchemaProperty schema{"Length", ValueKind::kGridLengths, {}, {}, kSchemaPaint};
    const auto parsed = parseValue(schema, value.asString());
    if (!std::holds_alternative<PropertyValue>(parsed))
        return {};
    const auto& lengths = std::get<PropertyValue>(parsed).asGridLengths();
    if (lengths.size() != 1 || !std::isfinite(lengths[0].value) || lengths[0].value < 0 ||
        lengths[0].value > std::numeric_limits<float>::max())
        return {};
    return lengths[0];
}
} // namespace
std::string TwoPaneView::className() const
{
    return "Velora.Fluent.TwoPaneView";
}
const XamlTypeDeclaration& TwoPaneView::declaration()
{
    return paneDeclaration("Velora.Fluent.TwoPaneView");
}
std::string TwoPaneView::registerType(Framework& ui)
{
    return registerLayout(ui, declaration(), [] { return createElement<TwoPaneView>(); });
}
const PropertyDescriptor& TwoPaneView::pane1Property()
{
    static PropertyRegistry registry;
    static const auto& property = registry.define("TwoPaneView.Pane1", ObjectRef{}, PropertyFlags::kPaint);
    return property;
}
const PropertyDescriptor& TwoPaneView::pane2Property()
{
    static PropertyRegistry registry;
    static const auto& property = registry.define("TwoPaneView.Pane2", ObjectRef{}, PropertyFlags::kPaint);
    return property;
}
const PropertyDescriptor& TwoPaneView::paneProperty(usize index) const
{
    return index == 0 ? pane1Property() : pane2Property();
}
std::shared_ptr<UIElement> TwoPaneView::pane1() const
{
    return paneObject(0);
}
PropertySetResult TwoPaneView::setPane1(std::shared_ptr<UIElement> value)
{
    return setPaneObject(0, std::move(value));
}
std::shared_ptr<UIElement> TwoPaneView::pane2() const
{
    return paneObject(1);
}
PropertySetResult TwoPaneView::setPane2(std::shared_ptr<UIElement> value)
{
    return setPaneObject(1, std::move(value));
}
const PropertyDescriptor& TwoPaneView::modeProperty()
{
    return modeKey().property();
}
TwoPaneViewMode TwoPaneView::mode() const
{
    const auto& value = getValue(modeProperty()).asString();
    return value == "Wide" ? TwoPaneViewMode::kWide
                           : (value == "Tall" ? TwoPaneViewMode::kTall : TwoPaneViewMode::kSinglePane);
}
u64 TwoPaneView::addModeChanged(std::function<void(TwoPaneViewMode)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [this, handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p,
                                             const PropertyValue&, const PropertyValue&)
        {
            if (p.id == modeProperty().id && isInTree())
                handler(mode());
        });
}
#define VELORA_PANES_ACCESSOR(cpp, xaml, type, getter)                                                       \
    type TwoPaneView::cpp() const                                                                            \
    {                                                                                                        \
        return getValue(layoutProperty(className(), #xaml)).getter();                                        \
    }                                                                                                        \
    PropertySetResult TwoPaneView::set##xaml(type value)                                                     \
    {                                                                                                        \
        return setValue(layoutProperty(className(), #xaml), std::move(value));                               \
    }
VELORA_PANES_ACCESSOR(minWideModeWidth, MinWideModeWidth, f64, asDouble)
VELORA_PANES_ACCESSOR(minTallModeHeight, MinTallModeHeight, f64, asDouble)
VELORA_PANES_ACCESSOR(panePriority, PanePriority, std::string, asString)
VELORA_PANES_ACCESSOR(wideModeConfiguration, WideModeConfiguration, std::string, asString)
VELORA_PANES_ACCESSOR(tallModeConfiguration, TallModeConfiguration, std::string, asString)
VELORA_PANES_ACCESSOR(pane1Length, Pane1Length, std::string, asString)
VELORA_PANES_ACCESSOR(pane2Length, Pane2Length, std::string, asString)
#undef VELORA_PANES_ACCESSOR
bool TwoPaneView::validateLayoutProperty(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == layoutProperty(className(), "MinWideModeWidth").id ||
        p.id == layoutProperty(className(), "MinTallModeHeight").id)
        return value.isDouble() && std::isfinite(value.asDouble()) && value.asDouble() >= 0 &&
               value.asDouble() <= std::numeric_limits<float>::max();
    if (p.id == layoutProperty(className(), "PanePriority").id)
        return value.isString() && (value.asString() == "Pane1" || value.asString() == "Pane2");
    if (p.id == layoutProperty(className(), "WideModeConfiguration").id)
        return value.isString() && (value.asString() == "SinglePane" || value.asString() == "LeftRight" ||
                                    value.asString() == "RightLeft");
    if (p.id == layoutProperty(className(), "TallModeConfiguration").id)
        return value.isString() && (value.asString() == "SinglePane" || value.asString() == "TopBottom" ||
                                    value.asString() == "BottomTop");
    if (p.id == layoutProperty(className(), "Pane1Length").id ||
        p.id == layoutProperty(className(), "Pane2Length").id)
        return parseLength(value).has_value();
    return true;
}
PaneLayoutPlan TwoPaneView::layoutPanes(Size available, const std::array<Size, 2>& desired)
{
    // WinUI 2.3 checks wide first, then tall, with strict greater-than thresholds.
    const bool wide = std::isfinite(available.width) && available.width > minWideModeWidth() &&
                      wideModeConfiguration() != "SinglePane";
    const bool tall = !wide && std::isfinite(available.height) && available.height > minTallModeHeight() &&
                      tallModeConfiguration() != "SinglePane";
    PaneLayoutPlan plan;
    plan.size = {std::isfinite(available.width) ? available.width
                                                : (wide ? desired[0].width + desired[1].width
                                                        : std::max(desired[0].width, desired[1].width)),
                 std::isfinite(available.height) ? available.height
                                                 : (tall ? desired[0].height + desired[1].height
                                                         : std::max(desired[0].height, desired[1].height))};
    if (!wide && !tall)
    {
        const usize chosen = panePriority() == "Pane1" ? 0 : 1;
        plan.visible[1 - chosen] = false;
        plan.panes[chosen] = {0, 0, plan.size.width, plan.size.height};
    }
    else
    {
        const auto first = *parseLength(pane1Length());
        const auto second = *parseLength(pane2Length());
        const float total = wide ? plan.size.width : plan.size.height;
        const auto fixed = [&](const GridLength& length, usize index)
        {
            if (length.unit == GridLength::Unit::kStar)
                return 0.f;
            return length.unit == GridLength::Unit::kPixel
                       ? static_cast<float>(length.value)
                       : (wide ? desired[index].width : desired[index].height);
        };
        const float fixed1 = std::min(total, fixed(first, 0));
        const float fixed2 = std::min(std::max(0.f, total - fixed1), fixed(second, 1));
        const float room = std::max(0.f, total - fixed1 - fixed2);
        const double weight1 = first.unit == GridLength::Unit::kStar ? first.value : 0;
        const double weight2 = second.unit == GridLength::Unit::kStar ? second.value : 0;
        const double weights = weight1 + weight2;
        const float size1 = fixed1 + (weights > 0 ? room * static_cast<float>(weight1 / weights) : 0);
        const float size2 = fixed2 + (weights > 0 ? room * static_cast<float>(weight2 / weights) : 0);
        const bool reverse =
            wide ? wideModeConfiguration() == "RightLeft" : tallModeConfiguration() == "BottomTop";
        const float offset1 = reverse ? size2 : 0;
        const float offset2 = reverse ? 0 : size1;
        plan.panes[0] =
            wide ? Box{offset1, 0, size1, plan.size.height} : Box{0, offset1, plan.size.width, size1};
        plan.panes[1] =
            wide ? Box{offset2, 0, size2, plan.size.height} : Box{0, offset2, plan.size.width, size2};
    }
    plan.mode = wide ? "Wide" : (tall ? "Tall" : "SinglePane");
    return plan;
}
void TwoPaneView::onPlanArranged(const PaneLayoutPlan& plan)
{
    setValue(modeKey(), std::string(plan.mode));
}
} // namespace velora
