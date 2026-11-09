#pragma once

#include <velora/controls/UIElement.hpp>

// Built-in element types and their layout (R05.3). Each override is the branch of the former
// Framework::measureContent/arrangeContent string dispatch for that type, moved unchanged.

namespace velora
{

// ---- Panels (Panels.cpp) ---------------------------------------------------------------------

class StackPanelElement final : public UIElement
{
public:
    StackPanelElement() noexcept : UIElement(ElementKind::kStackPanel) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

class GridElement final : public UIElement
{
public:
    GridElement() noexcept : UIElement(ElementKind::kGrid) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

class CanvasElement final : public UIElement
{
public:
    CanvasElement() noexcept : UIElement(ElementKind::kCanvas) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

// ---- Content hosts (ContentElements.cpp) -----------------------------------------------------

// Border, Page, UserControl: their string Content as text, else a single child.
class ContentHostElement final : public UIElement
{
public:
    explicit ContentHostElement(ElementKind kind) noexcept : UIElement(kind) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// Button and ContentPresenter: a template root, an object Content or text Content.
class ContentPresenterElement final : public UIElement
{
public:
    explicit ContentPresenterElement(ElementKind kind) noexcept : UIElement(kind) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

class ScrollViewerElement final : public UIElement
{
public:
    ScrollViewerElement() noexcept : UIElement(ElementKind::kScrollViewer) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

// ---- Text (TextAndFixedElements.cpp) -----------------------------------------------------------------

class TextBlockElement final : public UIElement
{
public:
    TextBlockElement() noexcept : UIElement(ElementKind::kTextBlock) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

class TextBoxElement final : public UIElement
{
public:
    TextBoxElement() noexcept : UIElement(ElementKind::kTextBox) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// ---- Fixed-look controls (TextAndFixedElements.cpp) -------------------------------------------------

// CheckBox and RadioButton: a 20-DIP glyph, a gap and the text content.
class ToggleButtonElement final : public UIElement
{
public:
    explicit ToggleButtonElement(ElementKind kind) noexcept : UIElement(kind) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

class ToggleSwitchElement final : public UIElement
{
public:
    ToggleSwitchElement() noexcept : UIElement(ElementKind::kToggleSwitch) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// Slider and ProgressBar.
class RangeBaseElement final : public UIElement
{
public:
    explicit RangeBaseElement(ElementKind kind) noexcept : UIElement(kind) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

class ImageElement final : public UIElement
{
public:
    ImageElement() noexcept : UIElement(ElementKind::kImage) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// ---- Items (TextAndFixedElements.cpp) -------------------------------------------------------------

class ListViewElement final : public UIElement
{
public:
    ListViewElement() noexcept : UIElement(ElementKind::kListView) {}
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

} // namespace velora
