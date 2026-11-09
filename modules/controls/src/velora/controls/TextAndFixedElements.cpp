#include <algorithm>
#include <cmath>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/LayoutContext.hpp>

namespace velora
{

// ---- TextBlock, TextBox ----------------------------------------------------------------------

Size TextBlockElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    if (context.text(e, context.props().FontFamily) == "Segoe MDL2 Assets")
    {
        const auto glyphs = context.text(e, context.props().Text);
        const auto count =
            std::count_if(glyphs.begin(), glyphs.end(), [](unsigned char c) { return (c & 0xC0) != 0x80; });
        return {input.fontSize * static_cast<f32>(count), lineHeight(input.fontSize)};
    }
    return {context.measureText(context.text(e, context.props().Text), input.fontSize),
            lineHeight(input.fontSize)};
}

Size TextBoxElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    const auto& p = context.props();
    const auto shown =
        context.text(e, p.Text).empty() ? context.text(e, p.PlaceholderText) : context.text(e, p.Text);
    return {context.measureText(shown, input.fontSize) + input.insetX,
            lineHeight(input.fontSize) + input.insetY};
}

// ---- CheckBox, RadioButton, ToggleSwitch, Slider, ProgressBar, Image ------------------------

Size ToggleButtonElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    // A 20-DIP glyph, an 8-DIP gap, then the text content.
    const auto label = context.text(e, context.props().Content);
    return {20 + (label.empty() ? 0 : 8 + context.measureText(label, input.fontSize)) + input.insetX,
            std::max(20.0f, lineHeight(input.fontSize)) + input.insetY};
}

Size ToggleSwitchElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    // Optional header line above a 40x20 switch followed by its On/Off content.
    const auto& p = context.props();
    const float font = input.fontSize;
    const auto header = context.text(e, p.Header);
    const float state = std::max(context.measureText(context.text(e, p.OnContent), font),
                                 context.measureText(context.text(e, p.OffContent), font));
    const float row = std::max(20.0f, lineHeight(font));
    return {std::max(header.empty() ? 0.0f : context.measureText(header, font), 40 + 12 + state) +
                input.insetX,
            (header.empty() ? 0 : lineHeight(font) + 4) + row + input.insetY};
}

Size RangeBaseElement::measureOverride(LayoutContext&, ElementId, const MeasureInput& input)
{
    return {std::min(120.0f, input.content.width) + input.insetX,
            (is(ElementKind::kSlider) ? 32.0f : 4.0f) + input.insetY};
}

Size ImageElement::measureOverride(LayoutContext&, ElementId, const MeasureInput&)
{
    return {};
}

// ---- ListView --------------------------------------------------------------------------------

Size ListViewElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    const auto& p = context.props();
    const Size content = input.content;
    const auto item = static_cast<float>(context.number(e, p.ItemHeight));
    for (const ElementId child : input.children)
        context.measure(child, {content.width, item});
    const float extent = item * static_cast<float>(context.property(e, p.ItemCount).asInt());
    return {std::isinf(content.width) ? 0 : content.width + input.insetX,
            (std::isinf(content.height) ? extent : std::min(extent, content.height)) + input.insetY};
}

void ListViewElement::arrangeOverride(LayoutContext& context, ElementId e, Box content,
                                      const std::vector<ElementId>& children)
{
    const auto& p = context.props();
    auto& viewport = context.listViewport(e);
    viewport.itemHeight = context.number(e, p.ItemHeight);
    viewport.count = static_cast<std::size_t>(std::max<i64>(0, context.property(e, p.ItemCount).asInt()));
    viewport.height = content.height;
    const double extent = viewport.itemHeight * static_cast<double>(viewport.count);
    viewport.offset =
        std::clamp(context.number(e, p.ScrollOffset), 0.0, std::max(0.0, extent - content.height));
    for (const ElementId child : children)
    {
        const auto index = static_cast<double>(context.property(child, p.ItemIndex).asInt());
        context.arrange(child, {content.x,
                                content.y + static_cast<float>(index * viewport.itemHeight - viewport.offset),
                                content.width, static_cast<float>(viewport.itemHeight)});
    }
}

} // namespace velora
