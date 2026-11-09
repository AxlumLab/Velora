#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <velora/controls/InputEvents.hpp>
#include <velora/controls/LayoutTypes.hpp>
#include <velora/runtime/FrameworkSchema.hpp>
#include <velora/runtime/property/DependencyObject.hpp>
#include <velora/types/TypeId.hpp>

// The controls core (R05.1): every element of a UI tree is a UIElement — a DependencyObject that
// knows its type and implements its own layout.
//
// Type hierarchy (logical, as registered in the RuntimeTypeRegistry; ControlTypes.cpp):
//   UIElement → FrameworkElement → Panel → StackPanel | Grid | Canvas
//                                → Control → ContentControl → ButtonBase → Button
//                                                                      → ToggleButton → CheckBox |
//                                                                      RadioButton
//                                                           → ScrollViewer | UserControl → Page
//                                          → TextBox | ToggleSwitch | RangeBase → Slider | ProgressBar
//                                          → ItemsControl → ListView
//                                → Border | TextBlock | Image | ContentPresenter
//
// ElementTree shares ownership of element objects; ordinary C++ virtual overrides provide
// their layout behavior.
//
// Measure/arrange overrides receive ElementId handles used by Framework and generated XAML.
// These handles belong to one tree and do not keep element objects alive.

namespace velora
{

class LayoutContext;
class InputContext;
class Canvas;
class Framework;
enum class AutomationControlType : u8;
using ElementId = u64;

// What MeasureOverride gets: the slot inside margin and explicit size, the same slot without padding
// and border (`content`), the padding+border insets, the font size and the visual children.
struct MeasureInput
{
    Size available;
    Size content;
    float insetX = 0, insetY = 0;
    float fontSize = 0;
    std::vector<ElementId> children;
};

class UIElement : public DependencyObject
{
  public:
    // `kind` is empty for elements of a type the built-in schema does not know (generic trees).
    explicit UIElement(std::optional<ElementKind> kind = std::nullopt) noexcept;
    virtual ~UIElement();

    [[nodiscard]] std::optional<ElementKind> kind() const noexcept
    {
        return m_kind;
    }
    [[nodiscard]] bool is(ElementKind kind) const noexcept
    {
        return m_kind == kind;
    }
    // "Velora.UI.<Type>", or empty for an untyped element. Source controls override this with
    // their stable declared class name; it must not change while the object is hosted.
    [[nodiscard]] virtual std::string className() const;
    [[nodiscard]] TypeId typeId() const;

    // FrameworkElement.MeasureOverride/ArrangeOverride. The default lays out the first child in the
    // whole content area (Border, Page, UserControl, unknown elements).
    virtual Size measureOverride(LayoutContext& context, ElementId self, const MeasureInput& input);
    virtual void arrangeOverride(LayoutContext& context, ElementId self, Box content,
                                 const std::vector<ElementId>& children);
    // Logical tree hosting, not a native window Loaded event. Failure during attach rolls the
    // insertion back; detach runs after removal with the object still alive.
    virtual void onAttached(Framework& ui, ElementId self);
    virtual void onDetached() noexcept;
    [[nodiscard]] virtual std::string automationName() const;
    [[nodiscard]] virtual std::optional<AutomationControlType> automationControlType() const noexcept;

    // Class input handling runs before this sender's instance handlers, along the same route.
    // Set handled to consume input. The host keeps this object alive during the callback.
    virtual void onPointerPressed(InputContext& context, PointerEventArgs& args);
    virtual void onPointerMoved(InputContext& context, PointerEventArgs& args);
    virtual void onPointerReleased(InputContext& context, PointerEventArgs& args);
    virtual void onKeyDown(InputContext& context, KeyEventArgs& args);
    // Custom controls opt into Tab navigation here; IsTabStop and ancestor state still apply.
    [[nodiscard]] virtual bool isFocusable() const noexcept;
    // Local-coordinate content, below children and after the background. Return true to replace
    // the registered built-in painter. Do not mutate the tree or properties while rendering.
    virtual bool renderOverride(Canvas& canvas, Size size) const;

    // True while a UI tree hosts this element. An element is in at most one tree (C-3.6).
    [[nodiscard]] bool isInTree() const noexcept
    {
        return m_host != nullptr;
    }

  private:
    friend class ElementTree;
    std::optional<ElementKind> m_kind;
    const void* m_host = nullptr; // the hosting ElementTree, set and cleared by it
};

// Creates the element object of `kind` (or a plain UIElement for an unknown type). Defaults of
// the type are not applied here; Framework applies them in the DefaultStyle layer.
[[nodiscard]] std::shared_ptr<UIElement> createElementObject(std::optional<ElementKind> kind);

// Behavioural categories that used to be string sets in Framework.
[[nodiscard]] constexpr bool isPressable(ElementKind kind) noexcept
{
    return kind == ElementKind::kButton || kind == ElementKind::kCheckBox ||
           kind == ElementKind::kRadioButton || kind == ElementKind::kToggleSwitch;
}
[[nodiscard]] constexpr bool isInteractive(ElementKind kind) noexcept
{
    return isPressable(kind) || kind == ElementKind::kTextBox || kind == ElementKind::kSlider;
}
[[nodiscard]] constexpr bool isRangeBase(ElementKind kind) noexcept
{
    return kind == ElementKind::kSlider || kind == ElementKind::kProgressBar;
}
// Elements whose string Content is drawn as their own text.
[[nodiscard]] constexpr bool showsTextContent(ElementKind kind) noexcept
{
    return kind == ElementKind::kButton || kind == ElementKind::kContentPresenter ||
           kind == ElementKind::kBorder || kind == ElementKind::kPage || kind == ElementKind::kUserControl ||
           kind == ElementKind::kScrollViewer;
}

} // namespace velora
