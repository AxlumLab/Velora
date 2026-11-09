#include <algorithm>
#include <limits>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/LayoutContext.hpp>

namespace velora
{

namespace
{
constexpr float kInfinity = std::numeric_limits<float>::infinity();

// String Content drawn as the element's own single line of text.
Size textContent(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    return {context.measureText(context.text(e, context.props().Content), input.fontSize) + input.insetX,
            lineHeight(input.fontSize) + input.insetY};
}

bool hasTextContent(LayoutContext& context, ElementId e)
{
    const auto* content = context.props().Content;
    return context.property(e, content).isString() && context.isSet(e, content);
}
} // namespace

// ---- Border, Page, UserControl ---------------------------------------------------------------

Size ContentHostElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    if (hasTextContent(context, e))
        return textContent(context, e, input);
    return UIElement::measureOverride(context, e, input);
}

// ---- Button, ContentPresenter ----------------------------------------------------------------

Size ContentPresenterElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    if (is(ElementKind::kButton) && context.property(e, context.props().HasTemplate).asBool())
        return input.children.empty()
                   ? Size{}
                   : context.measure(input.children.front(),
                                     input.available); // the template root fills the button
    if (const ElementId object = context.contentObject(e))
    {
        const auto size = context.measure(object, input.content);
        return {size.width + input.insetX, size.height + input.insetY};
    }
    return textContent(context, e, input);
}

// ---- ScrollViewer ----------------------------------------------------------------------------

Size ScrollViewerElement::measureOverride(LayoutContext& context, ElementId e, const MeasureInput& input)
{
    if (hasTextContent(context, e))
        return textContent(context, e, input);
    // A scrollable axis measures its content unconstrained; the viewer asks for no more than the
    // content needs and accepts any smaller slot.
    const auto& p = context.props();
    const Size content = input.content;
    Size child;
    if (!input.children.empty())
        child = context.measure(
            input.children.front(),
            {context.scrollable(e, p.HorizontalScrollBarVisibility) ? kInfinity : content.width,
             context.scrollable(e, p.VerticalScrollBarVisibility) ? kInfinity : content.height});
    return {std::min(child.width, content.width) + input.insetX,
            std::min(child.height, content.height) + input.insetY};
}

void ScrollViewerElement::arrangeOverride(LayoutContext& context, ElementId e, Box content,
                                          const std::vector<ElementId>& children)
{
    const auto& p = context.props();
    const Size desired = children.empty() ? Size{} : context.desired(children.front());
    const bool horizontal = context.scrollable(e, p.HorizontalScrollBarVisibility),
               vertical = context.scrollable(e, p.VerticalScrollBarVisibility);
    const float extentWidth = horizontal ? std::max(desired.width, content.width) : content.width;
    const float extentHeight = vertical ? std::max(desired.height, content.height) : content.height;
    const auto offset = [&](const PropertyDescriptor* d, float extent, float viewport)
    { return std::clamp(context.number(e, d), 0.0, std::max(0.0, double(extent - viewport))); };
    const double x = offset(p.HorizontalOffset, extentWidth, content.width),
                 y = offset(p.VerticalOffset, extentHeight, content.height);
    // A smaller extent pulls the offsets back into range; they report the actual position.
    if (context.number(e, p.HorizontalOffset) != x)
        context.set(e, p.HorizontalOffset, PropertyValue(x));
    if (context.number(e, p.VerticalOffset) != y)
        context.set(e, p.VerticalOffset, PropertyValue(y));
    context.set(e, p.ExtentWidth, PropertyValue(double(extentWidth)));
    context.set(e, p.ExtentHeight, PropertyValue(double(extentHeight)));
    context.set(e, p.ViewportWidth, PropertyValue(double(content.width)));
    context.set(e, p.ViewportHeight, PropertyValue(double(content.height)));
    if (!children.empty())
        context.arrange(children.front(), {content.x - static_cast<float>(x),
                                           content.y - static_cast<float>(y), extentWidth, extentHeight});
}

} // namespace velora
