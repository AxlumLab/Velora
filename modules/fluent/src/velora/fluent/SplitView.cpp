// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See modules/fluent/LICENSE-WinUI2.3.txt.
// System SplitView behavior implemented against Velora; template source: SplitView_themeresources.xaml.
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/fluent/SplitView.hpp>
#include <velora/runtime/Framework.hpp>

namespace velora
{
std::string SplitView::className() const
{
    return "Velora.Fluent.SplitView";
}
const XamlTypeDeclaration& SplitView::declaration()
{
    return paneDeclaration("Velora.Fluent.SplitView");
}
std::string SplitView::registerType(Framework& ui)
{
    return registerLayout(ui, declaration(), [] { return createElement<SplitView>(); });
}
const PropertyDescriptor& SplitView::paneObjectProperty()
{
    static PropertyRegistry registry;
    static const auto& property = registry.define("SplitView.Pane", ObjectRef{}, PropertyFlags::kPaint);
    return property;
}
const PropertyDescriptor& SplitView::paneProperty(usize index) const
{
    return index == 0 ? *elementProperties().props.Content : paneObjectProperty();
}
std::shared_ptr<UIElement> SplitView::content() const
{
    return paneObject(0);
}
PropertySetResult SplitView::setContent(std::shared_ptr<UIElement> value)
{
    return setPaneObject(0, std::move(value));
}
std::shared_ptr<UIElement> SplitView::pane() const
{
    return paneObject(1);
}
PropertySetResult SplitView::setPane(std::shared_ptr<UIElement> value)
{
    return setPaneObject(1, std::move(value));
}
const PropertyDescriptor& SplitView::isPaneOpenProperty()
{
    return layoutProperty("Velora.Fluent.SplitView", "IsPaneOpen");
}
bool SplitView::isPaneOpen() const
{
    return getValue(isPaneOpenProperty()).asBool();
}
PropertySetResult SplitView::setIsPaneOpen(bool value)
{
    return setValue(isPaneOpenProperty(), value);
}
#define VELORA_SPLIT_ACCESSOR(cpp, xaml, type, getter)                                                       \
    type SplitView::cpp() const                                                                              \
    {                                                                                                        \
        return getValue(layoutProperty(className(), #xaml)).getter();                                        \
    }                                                                                                        \
    PropertySetResult SplitView::set##xaml(type value)                                                       \
    {                                                                                                        \
        return setValue(layoutProperty(className(), #xaml), std::move(value));                               \
    }
VELORA_SPLIT_ACCESSOR(openPaneLength, OpenPaneLength, f64, asDouble)
VELORA_SPLIT_ACCESSOR(compactPaneLength, CompactPaneLength, f64, asDouble)
VELORA_SPLIT_ACCESSOR(displayMode, DisplayMode, std::string, asString)
VELORA_SPLIT_ACCESSOR(panePlacement, PanePlacement, std::string, asString)
#undef VELORA_SPLIT_ACCESSOR
bool SplitView::validateLayoutProperty(const PropertyDescriptor& p, const PropertyValue& value) const
{
    if (p.id == isPaneOpenProperty().id)
        return value.isBool();
    if (p.id == layoutProperty(className(), "OpenPaneLength").id ||
        p.id == layoutProperty(className(), "CompactPaneLength").id)
        return value.isDouble() && std::isfinite(value.asDouble()) && value.asDouble() >= 0 &&
               value.asDouble() <= std::numeric_limits<float>::max();
    if (p.id == layoutProperty(className(), "DisplayMode").id)
        return value.isString() &&
               (value.asString() == "Overlay" || value.asString() == "Inline" ||
                value.asString() == "CompactOverlay" || value.asString() == "CompactInline");
    if (p.id == layoutProperty(className(), "PanePlacement").id)
        return value.isString() && (value.asString() == "Left" || value.asString() == "Right");
    return true;
}
PaneLayoutPlan SplitView::layoutPanes(Size available, const std::array<Size, 2>& desired)
{
    const auto display = displayMode();
    const bool compact = display == "CompactInline" || display == "CompactOverlay";
    const bool inlineMode = display == "Inline" || display == "CompactInline";
    const float compactLength = compact ? static_cast<float>(compactPaneLength()) : 0;
    const float length = isPaneOpen() ? static_cast<float>(openPaneLength()) : compactLength;
    const float reserved = inlineMode ? length : compactLength;
    PaneLayoutPlan plan;
    plan.size = {std::isfinite(available.width) ? available.width : desired[0].width + reserved,
                 std::isfinite(available.height) ? available.height
                                                 : std::max(desired[0].height, desired[1].height)};
    const float occupied = std::min(reserved, plan.size.width);
    const float paneWidth = std::min(length, plan.size.width);
    const bool right = panePlacement() == "Right";
    plan.panes[0] = {right ? 0.f : occupied, 0, std::max(0.f, plan.size.width - occupied), plan.size.height};
    plan.panes[1] = {right ? plan.size.width - paneWidth : 0.f, 0, paneWidth, plan.size.height};
    plan.visible[1] = paneWidth > 0;
    plan.dismissOverlay = isPaneOpen() && !inlineMode;
    return plan;
}
void SplitView::onPointerPressed(InputContext&, PointerEventArgs& args)
{
    auto* ui = attachedFramework();
    if (!ui || !isPaneOpen() || (displayMode() != "Overlay" && displayMode() != "CompactOverlay"))
        return;
    if (ui->isWithin(args.originalSource, paneHost(1)))
        return;
    setIsPaneOpen(false);
    args.handled = true;
}
void SplitView::onKeyDown(InputContext&, KeyEventArgs& args)
{
    if (args.key == Key::kEscape && isPaneOpen())
    {
        setIsPaneOpen(false);
        args.handled = true;
    }
}
} // namespace velora
