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
    StackPanelElement() noexcept : UIElement(ElementKind::kStackPanel)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

class GridElement final : public UIElement
{
  public:
    GridElement() noexcept : UIElement(ElementKind::kGrid)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

class CanvasElement final : public UIElement
{
  public:
    CanvasElement() noexcept : UIElement(ElementKind::kCanvas)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

// ---- Content hosts (ContentElements.cpp) -----------------------------------------------------

// Border, Page, UserControl: their string Content as text, else a single child.
class ContentHostElement final : public UIElement
{
  public:
    explicit ContentHostElement(ElementKind kind) noexcept : UIElement(kind)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// Button and ContentPresenter: a template root, an object Content or text Content.
class ContentPresenterElement final : public UIElement
{
  public:
    explicit ContentPresenterElement(ElementKind kind) noexcept : UIElement(kind)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

class ScrollViewerElement final : public UIElement
{
  public:
    ScrollViewerElement() noexcept : UIElement(ElementKind::kScrollViewer)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

// ---- Text (TextAndFixedElements.cpp) -----------------------------------------------------------------

class TextBlockElement final : public UIElement
{
  public:
    TextBlockElement() noexcept : UIElement(ElementKind::kTextBlock)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

class TextBoxElement final : public UIElement
{
  public:
    TextBoxElement() noexcept : UIElement(ElementKind::kTextBox)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// ---- Fixed-look controls (TextAndFixedElements.cpp) -------------------------------------------------

// CheckBox and RadioButton: a 20-DIP glyph, a gap and the text content.
// Boolean state only; nullable IsChecked/three-state behavior is not implemented yet.
class ToggleButtonElement final : public UIElement
{
  public:
    explicit ToggleButtonElement(ElementKind kind) noexcept : UIElement(kind)
    {
    }
    [[nodiscard]] bool isChecked() const;
    PropertySetResult setIsChecked(bool value, PropertySource source = PropertySource::kLocal);
    [[nodiscard]] static const PropertyDescriptor& isCheckedProperty();
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

class ToggleSwitchElement final : public UIElement
{
  public:
    ToggleSwitchElement() noexcept : UIElement(ElementKind::kToggleSwitch)
    {
    }
    [[nodiscard]] bool isOn() const;
    PropertySetResult setIsOn(bool value, PropertySource source = PropertySource::kLocal);
    [[nodiscard]] static const PropertyDescriptor& isOnProperty();
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// Slider and ProgressBar.
class RangeBaseElement : public UIElement
{
  public:
    explicit RangeBaseElement(ElementKind kind) noexcept : UIElement(kind)
    {
    }
    using DependencyObject::setValue;
    [[nodiscard]] f64 minimum() const;
    [[nodiscard]] f64 maximum() const;
    [[nodiscard]] f64 value() const;
    PropertySetResult setMinimum(f64 value, PropertySource source = PropertySource::kLocal);
    PropertySetResult setMaximum(f64 value, PropertySource source = PropertySource::kLocal);
    PropertySetResult setValue(f64 value, PropertySource source = PropertySource::kLocal);
    [[nodiscard]] static const PropertyDescriptor& minimumProperty();
    [[nodiscard]] static const PropertyDescriptor& maximumProperty();
    [[nodiscard]] static const PropertyDescriptor& valueProperty();
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;

  protected:
    bool validatePropertyValue(const PropertyDescriptor&, const PropertyValue&) const override;
    void normalizePropertyValues(const PropertyDescriptor&) override;
};

class SliderElement final : public RangeBaseElement
{
  public:
    SliderElement() noexcept : RangeBaseElement(ElementKind::kSlider)
    {
    }
    [[nodiscard]] f64 stepFrequency() const;
    PropertySetResult setStepFrequency(f64 value, PropertySource source = PropertySource::kLocal);
    [[nodiscard]] static const PropertyDescriptor& stepFrequencyProperty();
};

class ProgressBarElement final : public RangeBaseElement
{
  public:
    ProgressBarElement() noexcept : RangeBaseElement(ElementKind::kProgressBar)
    {
    }
};

class ImageElement final : public UIElement
{
  public:
    ImageElement() noexcept : UIElement(ElementKind::kImage)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
};

// ---- Items (TextAndFixedElements.cpp) -------------------------------------------------------------

class ListViewElement final : public UIElement
{
  public:
    ListViewElement() noexcept : UIElement(ElementKind::kListView)
    {
    }
    Size measureOverride(LayoutContext&, ElementId, const MeasureInput&) override;
    void arrangeOverride(LayoutContext&, ElementId, Box, const std::vector<ElementId>&) override;
};

} // namespace velora
