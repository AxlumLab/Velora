#include <algorithm>
#include <cmath>
#include <limits>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/LayoutContext.hpp>

namespace velora
{

namespace
{
constexpr float kInfinity = std::numeric_limits<float>::infinity();

GridLengths lengths(LayoutContext& context, ElementId e, const PropertyDescriptor* d)
{
    const auto& v = context.property(e, d);
    return v.empty() || v.asGridLengths().empty() ? GridLengths{GridLength{}} : v.asGridLengths();
}
} // namespace

// ---- StackPanel ------------------------------------------------------------------------------

Size StackPanelElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    const auto& p = context.props();
    const bool horizontal = context.text(e, p.Orientation) == "Horizontal";
    const auto spacing = static_cast<float>(context.number(e, p.Spacing));
    const Size content = input.content;
    Size total;
    std::size_t shown = 0;
    for (const ElementId child : input.children)
    {
        const Size d = context.measure(child, horizontal ? Size{kInfinity, content.height}
                                                         : Size{content.width, kInfinity});
        if (d.width == 0 && d.height == 0)
            continue;
        ++shown;
        if (horizontal)
        {
            total.width += d.width;
            total.height = std::max(total.height, d.height);
        }
        else
        {
            total.height += d.height;
            total.width = std::max(total.width, d.width);
        }
    }
    const float gaps = shown > 1 ? spacing * static_cast<float>(shown - 1) : 0;
    (horizontal ? total.width : total.height) += gaps;
    return {total.width + input.insetX, total.height + input.insetY};
}

void StackPanelElement::arrangeOverride(LayoutContext& context, ElementId e, Box content,
                                        const std::vector<ElementId>& children)
{
    const auto& p = context.props();
    const bool horizontal = context.text(e, p.Orientation) == "Horizontal";
    const auto spacing = static_cast<float>(context.number(e, p.Spacing));
    float cursor = horizontal ? content.x : content.y;
    for (const ElementId child : children)
    {
        const Size d = context.desired(child);
        if (horizontal)
            context.arrange(child, {cursor, content.y, d.width, content.height});
        else
            context.arrange(child, {content.x, cursor, content.width, d.height});
        if (d.width > 0 || d.height > 0)
            cursor += (horizontal ? d.width : d.height) + spacing;
    }
}

// ---- Grid ------------------------------------------------------------------------------------

Size GridElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    const auto& p = context.props();
    const GridLengths rows = lengths(context, e, p.RowDefinitions);
    const GridLengths cols = lengths(context, e, p.ColumnDefinitions);
    std::vector<float> rowSize(rows.size()), colSize(cols.size());
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].unit == GridLength::Unit::kPixel)
            rowSize[i] = static_cast<float>(rows[i].value);
    for (std::size_t i = 0; i < cols.size(); ++i)
        if (cols[i].unit == GridLength::Unit::kPixel)
            colSize[i] = static_cast<float>(cols[i].value);
    for (const ElementId child : input.children)
    {
        const Size d = context.measure(child, input.content);
        const auto r = std::clamp<std::size_t>(
            static_cast<std::size_t>(std::max(0.0, context.number(child, p.GridRow))), 0, rows.size() - 1);
        const auto c = std::clamp<std::size_t>(
            static_cast<std::size_t>(std::max(0.0, context.number(child, p.GridColumn))), 0, cols.size() - 1);
        // Spanning children do not size Auto/Star tracks in this subset.
        if (context.number(child, p.GridRowSpan) <= 1 && rows[r].unit != GridLength::Unit::kPixel)
            rowSize[r] = std::max(rowSize[r], d.height);
        if (context.number(child, p.GridColumnSpan) <= 1 && cols[c].unit != GridLength::Unit::kPixel)
            colSize[c] = std::max(colSize[c], d.width);
    }
    const auto sum = [](const std::vector<float>& v, double gap)
    {
        float s = 0;
        for (float x : v)
            s += x;
        return s + (v.size() > 1 ? static_cast<float>(gap) * static_cast<float>(v.size() - 1) : 0.0f);
    };
    return {sum(colSize, context.number(e, p.ColumnSpacing)) + input.insetX,
            sum(rowSize, context.number(e, p.RowSpacing)) + input.insetY};
}

void GridElement::arrangeOverride(LayoutContext& context, ElementId e, Box content,
                                  const std::vector<ElementId>& children)
{
    const auto& p = context.props();
    const GridLengths rows = lengths(context, e, p.RowDefinitions),
                      cols = lengths(context, e, p.ColumnDefinitions);
    const auto tracks = [&](const GridLengths& defs, float total, double gap, bool isRow)
    {
        std::vector<float> size(defs.size());
        for (std::size_t i = 0; i < defs.size(); ++i)
        {
            if (defs[i].unit == GridLength::Unit::kPixel)
                size[i] = static_cast<float>(defs[i].value);
            if (defs[i].unit != GridLength::Unit::kAuto)
                continue;
            for (const ElementId child : children)
            {
                const double index = context.number(child, isRow ? p.GridRow : p.GridColumn);
                if (static_cast<std::size_t>(std::max(0.0, index)) == i)
                    size[i] = std::max(size[i],
                                       isRow ? context.desired(child).height : context.desired(child).width);
            }
        }
        float used = defs.size() > 1 ? static_cast<float>(gap) * static_cast<float>(defs.size() - 1) : 0.0f;
        double weights = 0;
        for (std::size_t i = 0; i < defs.size(); ++i)
        {
            if (defs[i].unit == GridLength::Unit::kStar)
                weights += defs[i].value;
            else
                used += size[i];
        }
        for (std::size_t i = 0; i < defs.size(); ++i)
            if (defs[i].unit == GridLength::Unit::kStar)
                size[i] = std::max(0.0f, total - used) * static_cast<float>(defs[i].value / weights);
        return size;
    };
    const auto rowGap = static_cast<float>(context.number(e, p.RowSpacing)),
               colGap = static_cast<float>(context.number(e, p.ColumnSpacing));
    const auto rowSize = tracks(rows, content.height, rowGap, true),
               colSize = tracks(cols, content.width, colGap, false);
    const auto span = [](const std::vector<float>& sizes, float gap, std::size_t first, std::size_t count,
                         float& offset, float& length)
    {
        offset = 0;
        for (std::size_t i = 0; i < first; ++i)
            offset += sizes[i] + gap;
        length = 0;
        for (std::size_t i = first; i < std::min(sizes.size(), first + count); ++i)
            length += sizes[i] + (i > first ? gap : 0);
    };
    for (const ElementId child : children)
    {
        const auto r = std::min<std::size_t>(
            static_cast<std::size_t>(std::max(0.0, context.number(child, p.GridRow))), rows.size() - 1);
        const auto c = std::min<std::size_t>(
            static_cast<std::size_t>(std::max(0.0, context.number(child, p.GridColumn))), cols.size() - 1);
        float ox = 0, oy = 0, w = 0, h = 0;
        span(colSize, colGap, c,
             static_cast<std::size_t>(std::max(1.0, context.number(child, p.GridColumnSpan))), ox, w);
        span(rowSize, rowGap, r,
             static_cast<std::size_t>(std::max(1.0, context.number(child, p.GridRowSpan))), oy, h);
        context.arrange(child, {content.x + ox, content.y + oy, w, h});
    }
}

// ---- Canvas ----------------------------------------------------------------------------------

Size CanvasElement::measureOverride(LayoutContext& context, ElementId, const MeasureInput& input)
{
    // Children size themselves; the Canvas itself asks for nothing.
    for (const ElementId child : input.children)
        context.measure(child, {kInfinity, kInfinity});
    return {input.insetX, input.insetY};
}

void CanvasElement::arrangeOverride(LayoutContext& context, ElementId, Box content,
                                    const std::vector<ElementId>& children)
{
    const auto& p = context.props();
    for (const ElementId child : children)
        context.arrange(child, {content.x + static_cast<float>(context.number(child, p.CanvasLeft)),
                                content.y + static_cast<float>(context.number(child, p.CanvasTop)),
                                context.desired(child).width, context.desired(child).height});
}

} // namespace velora
