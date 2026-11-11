// Copyright (c) Microsoft Corporation. All rights reserved. MIT License.
// Velora system ScrollBar behavior; WinUI 2.3 provides ScrollBar_themeresources.xaml only.
#include <algorithm>
#include <cmath>
#include <utility>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/InputContext.hpp>
#include <velora/fluent/ScrollBar.hpp>
#include <velora/graphics/Canvas.hpp>
namespace velora
{
namespace
{
constexpr std::string_view kType = "Velora.Fluent.ScrollBar";
}
ScrollBar::ScrollBar() : RangeBaseElement(ElementKind::kSlider)
{
}
std::string ScrollBar::className() const
{
    return std::string(kType);
}
const XamlTypeDeclaration& ScrollBar::declaration()
{
    return fluentDeclaration(kType);
}
std::string ScrollBar::registerType(Framework& ui)
{
    return registerFluentControl(ui, kType, [] { return createElement<ScrollBar>(); });
}
#define VELORA_SCROLL_FIELD(Name, Setter)                                                                    \
    f64 ScrollBar::Name() const                                                                              \
    {                                                                                                        \
        return getValue(fluentProperty(kType, #Setter)).asDouble();                                          \
    }                                                                                                        \
    PropertySetResult ScrollBar::set##Setter(f64 value)                                                      \
    {                                                                                                        \
        return setValue(fluentProperty(kType, #Setter), value);                                              \
    }
VELORA_SCROLL_FIELD(viewportSize, ViewportSize)
VELORA_SCROLL_FIELD(smallChange, SmallChange)
VELORA_SCROLL_FIELD(largeChange, LargeChange)
#undef VELORA_SCROLL_FIELD
bool ScrollBar::vertical() const
{
    return getValue(fluentProperty(kType, "Orientation")).asString() == "Vertical";
}
bool ScrollBar::validatePropertyValue(const PropertyDescriptor& p, const PropertyValue& v) const
{
    if (p.id == fluentProperty(kType, "Orientation").id)
        return v.isString() && (v.asString() == "Vertical" || v.asString() == "Horizontal");
    for (const auto& d : declaration().properties)
        if (p.id == d.id())
            return v.isDouble() && std::isfinite(v.asDouble()) && v.asDouble() >= 0;
    return RangeBaseElement::validatePropertyValue(p, v);
}
Size ScrollBar::measureOverride(LayoutContext&, ElementId, const MeasureInput&)
{
    return vertical() ? Size{20, 160} : Size{160, 20};
}
std::pair<f64, f64> ScrollBar::thumb(f64 length) const
{
    const auto track = std::max(0.0, length - 32);
    const auto range = maximum() - minimum();
    const auto extent = range + viewportSize();
    const auto size = std::min(track, std::max(16.0, extent > 0 ? track * viewportSize() / extent : track));
    return {16 + (range > 0 ? (track - size) * (value() - minimum()) / range : 0), size};
}
bool ScrollBar::renderOverride(Canvas& canvas, Size size) const
{
    const bool v = vertical();
    const auto [start, length] = thumb(v ? size.height : size.width);
    canvas.drawRect({0, 0, size.width, size.height}, Paint{Color{.94f, .94f, .94f, 1}});
    canvas.drawRoundedRect(
        v ? Rect{5, static_cast<f32>(start), std::max(0.0f, size.width - 10), static_cast<f32>(length)}
          : Rect{static_cast<f32>(start), 5, static_cast<f32>(length), std::max(0.0f, size.height - 10)},
        3, Paint{Color{.4f, .4f, .4f, 1}});
    canvas.drawText({0, 0, v ? size.width : 16, v ? 16 : size.height}, v ? "\xEE\x9C\x8E" : "\xEE\x9D\xAB",
                    10, Paint{Color{.2f, .2f, .2f, 1}}, "Segoe MDL2 Assets");
    canvas.drawText(v ? Rect{0, std::max(0.0f, size.height - 16), size.width, 16}
                      : Rect{std::max(0.0f, size.width - 16), 0, 16, size.height},
                    v ? "\xEE\x9C\x8D" : "\xEE\x9D\xAC", 10, Paint{Color{.2f, .2f, .2f, 1}},
                    "Segoe MDL2 Assets");
    return true;
}
bool ScrollBar::isFocusable() const noexcept
{
    return true;
}
void ScrollBar::onPointerPressed(InputContext& context, PointerEventArgs& args)
{
    const auto bounds = context.bounds();
    const f64 length = vertical() ? bounds.height : bounds.width;
    const f64 coordinate = vertical() ? args.y : args.x;
    const auto [start, size] = thumb(length);
    context.focus();
    args.handled = true;
    if (coordinate < 16)
        setValue(value() - smallChange());
    else if (coordinate > length - 16)
        setValue(value() + smallChange());
    else if (coordinate < start)
        setValue(value() - largeChange());
    else if (coordinate > start + size)
        setValue(value() + largeChange());
    else if (context.capturePointer())
    {
        m_dragging = true;
        m_grab = coordinate - start;
    }
}
void ScrollBar::onPointerMoved(InputContext& context, PointerEventArgs& args)
{
    if (!m_dragging)
        return;
    if (!context.hasPointerCapture())
    {
        m_dragging = false;
        return;
    }
    const auto bounds = context.bounds();
    const f64 length = vertical() ? bounds.height : bounds.width;
    const auto [start, size] = thumb(length);
    (void)start;
    const auto travel = std::max(0.0, length - 32 - size);
    const f64 coordinate = vertical() ? args.y : args.x;
    if (travel > 0)
        setValue(minimum() +
                 (maximum() - minimum()) * std::clamp((coordinate - 16 - m_grab) / travel, 0.0, 1.0));
    args.handled = true;
}
void ScrollBar::onPointerReleased(InputContext& context, PointerEventArgs& args)
{
    if (!m_dragging)
        return;
    m_dragging = false;
    if (context.hasPointerCapture())
        context.releasePointer();
    args.handled = true;
}
void ScrollBar::onDetached() noexcept
{
    m_dragging = false;
}
void ScrollBar::onKeyDown(InputContext&, KeyEventArgs& args)
{
    switch (args.key)
    {
    case Key::kLeft:
    case Key::kUp:
        setValue(value() - smallChange());
        break;
    case Key::kRight:
    case Key::kDown:
        setValue(value() + smallChange());
        break;
    case Key::kPageUp:
        setValue(value() - largeChange());
        break;
    case Key::kPageDown:
        setValue(value() + largeChange());
        break;
    case Key::kHome:
        setValue(minimum());
        break;
    case Key::kEnd:
        setValue(maximum());
        break;
    default:
        return;
    }
    args.handled = true;
}
u64 ScrollBar::addValueChanged(std::function<void(f64, f64)> handler)
{
    if (!handler)
        return 0;
    return addPropertyChanged(
        [handler = std::move(handler)](DependencyObject&, const PropertyDescriptor& p, const PropertyValue& a,
                                       const PropertyValue& b)
        {
            if (p.id == valueProperty().id)
                handler(a.asDouble(), b.asDouble());
        });
}
} // namespace velora
