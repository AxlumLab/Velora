#include "ValuePad.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <velora/controls/InputContext.hpp>
#include <velora/graphics/Canvas.hpp>

namespace velora
{
ValuePad::ValuePad() noexcept : UIElement(ElementKind::kUserControl)
{
}

const PropertyDescriptor& ValuePad::valueProperty()
{
    static PropertyRegistry registry;
    static const auto& property =
        registry.define("ValuePad.Value", PropertyValue(0.0), PropertyFlags::kPaint);
    return property;
}

f64 ValuePad::value() const
{
    return getValue(valueProperty()).asDouble();
}

PropertySetResult ValuePad::setValue(f64 value)
{
    return setValue(valueProperty(), PropertyValue(value));
}

bool ValuePad::validatePropertyValue(const PropertyDescriptor& property, const PropertyValue& value) const
{
    return property.id != valueProperty().id || (value.isDouble() && std::isfinite(value.asDouble()));
}

void ValuePad::normalizePropertyValues(const PropertyDescriptor& property)
{
    if (property.id == valueProperty().id)
        normalizeValue(valueProperty(), PropertyValue(std::clamp(value(), 0.0, 100.0)));
}

Size ValuePad::measureOverride(LayoutContext&, ElementId, const MeasureInput& input)
{
    return {240 + input.insetX, 64 + input.insetY};
}

bool ValuePad::isFocusable() const noexcept
{
    return true;
}

void ValuePad::setFromPointer(InputContext& context, const PointerEventArgs& args)
{
    const f64 width = std::max(1.0f, context.bounds().width);
    setValue(100 * args.x / width);
}

void ValuePad::onPointerPressed(InputContext& context, PointerEventArgs& args)
{
    context.focus();
    context.capturePointer();
    setFromPointer(context, args);
    args.handled = true;
}

void ValuePad::onPointerMoved(InputContext& context, PointerEventArgs& args)
{
    if (context.hasPointerCapture())
    {
        setFromPointer(context, args);
        args.handled = true;
    }
}

void ValuePad::onPointerReleased(InputContext& context, PointerEventArgs& args)
{
    if (context.hasPointerCapture())
    {
        setFromPointer(context, args);
        context.releasePointer();
        args.handled = true;
    }
}

void ValuePad::onKeyDown(InputContext&, KeyEventArgs& args)
{
    const f64 step = args.shift ? 10 : 1;
    if (args.key == Key::kLeft)
        setValue(value() - step);
    else if (args.key == Key::kRight)
        setValue(value() + step);
    else if (args.key == Key::kHome)
        setValue(0);
    else if (args.key == Key::kEnd)
        setValue(100);
    else
        return;
    args.handled = true;
}

bool ValuePad::renderOverride(Canvas& canvas, Size size) const
{
    const f64 amount = value();
    canvas.drawRoundedRect({0, 0, size.width, size.height}, 8, {{0.12f, 0.15f, 0.2f, 1}});
    canvas.drawRect({8, size.height - 12, (size.width - 16) * static_cast<f32>(amount / 100), 4},
                    {{0.25f, 0.7f, 1, 1}});
    canvas.drawText({12, 8, size.width - 24, 32}, "Value " + std::to_string(static_cast<i32>(amount)), 20,
                    {{1, 1, 1, 1}});
    return true;
}
} // namespace velora
