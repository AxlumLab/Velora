#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/LayoutContext.hpp>

namespace velora
{

UIElement::UIElement(std::optional<ElementKind> kind) noexcept : m_kind(kind)
{
}

UIElement::~UIElement() = default;

void UIElement::onAttached(Framework&, ElementId)
{
}

void UIElement::onDetached() noexcept
{
}

std::string UIElement::automationName() const
{
    return {};
}

std::optional<AutomationControlType> UIElement::automationControlType() const noexcept
{
    return std::nullopt;
}

void UIElement::onPointerPressed(InputContext&, PointerEventArgs&)
{
}

void UIElement::onPointerMoved(InputContext&, PointerEventArgs&)
{
}

void UIElement::onPointerReleased(InputContext&, PointerEventArgs&)
{
}

void UIElement::onKeyDown(InputContext&, KeyEventArgs&)
{
}

bool UIElement::renderOverride(Canvas&, Size) const
{
    return false;
}

bool UIElement::isFocusable() const noexcept
{
    return m_kind && isInteractive(*m_kind);
}

std::string UIElement::className() const
{
    return m_kind ? "Velora.UI." + std::string(schemaType(*m_kind).name) : std::string();
}

TypeId UIElement::typeId() const
{
    return m_kind ? typeIdFor(className()) : TypeId{};
}

Size UIElement::measureOverride(LayoutContext& context, ElementId, const MeasureInput& input)
{
    // A single child (Page, UserControl, Border and elements of unknown types).
    Size child;
    if (!input.children.empty())
        child = context.measure(input.children.front(), input.content);
    return {child.width + input.insetX, child.height + input.insetY};
}

void UIElement::arrangeOverride(LayoutContext& context, ElementId, Box content,
                                const std::vector<ElementId>& children)
{
    if (!children.empty())
        context.arrange(children.front(), content);
}

std::shared_ptr<UIElement> createElementObject(std::optional<ElementKind> kind)
{
    if (!kind)
        return std::make_shared<UIElement>();
    switch (*kind)
    {
    case ElementKind::kStackPanel:
        return std::make_shared<StackPanelElement>();
    case ElementKind::kGrid:
        return std::make_shared<GridElement>();
    case ElementKind::kCanvas:
        return std::make_shared<CanvasElement>();
    case ElementKind::kPage:
    case ElementKind::kUserControl:
    case ElementKind::kBorder:
        return std::make_shared<ContentHostElement>(*kind);
    case ElementKind::kButton:
    case ElementKind::kContentPresenter:
        return std::make_shared<ContentPresenterElement>(*kind);
    case ElementKind::kScrollViewer:
        return std::make_shared<ScrollViewerElement>();
    case ElementKind::kTextBlock:
        return std::make_shared<TextBlockElement>();
    case ElementKind::kTextBox:
        return std::make_shared<TextBoxElement>();
    case ElementKind::kCheckBox:
    case ElementKind::kRadioButton:
        return std::make_shared<ToggleButtonElement>(*kind);
    case ElementKind::kToggleSwitch:
        return std::make_shared<ToggleSwitchElement>();
    case ElementKind::kSlider:
        return std::make_shared<SliderElement>();
    case ElementKind::kProgressBar:
        return std::make_shared<ProgressBarElement>();
    case ElementKind::kImage:
        return std::make_shared<ImageElement>();
    case ElementKind::kListView:
        return std::make_shared<ListViewElement>();
    }
    return std::make_shared<UIElement>(kind);
}

} // namespace velora
