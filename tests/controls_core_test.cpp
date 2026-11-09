// R05 controls core: typed element objects, object-first creation (configure before any tree,
// insert keeping the values), per-type layout overrides, routed events with Handled and
// handledEventsToo, the control type hierarchy, and incremental invalidation.
#include <cstdio>
#include <string>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/ControlTypes.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/runtime/object/BuiltinTypes.hpp>

using namespace velora;

namespace
{
int g_failures = 0;

void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void typedObjects()
{
    Compositor compositor;
    Framework ui(compositor);
    const ElementId stack = ui.create(ElementKind::kStackPanel);
    const ElementId button = ui.create(ElementKind::kButton, stack);
    UIElement* element = ui.tree().uiElement(button);
    check(element && element->is(ElementKind::kButton) && element->className() == "Velora.UI.Button",
          "a tree node is a typed element object");
    check(dynamic_cast<StackPanelElement*>(ui.tree().uiElement(stack)) != nullptr,
          "each built-in type has its own element class");
    check(ui.tree().kind(button) == ElementKind::kButton && ui.tree().is(stack, ElementKind::kStackPanel),
          "the tree answers element kinds without type-name strings");
}

void objectFirst()
{
    const auto& p = elementProperties().props;
    // No Framework, window or root exists yet.
    auto button = createElement(ElementKind::kButton);
    check(!button->isInTree(), "a new element is not in any tree");
    check(button->valueSource(*p.Padding) == PropertySource::kDefaultStyle,
          "type defaults are applied off-tree");
    button->setValue(*p.Content, PropertyValue(std::string("Save")));
    button->setValue(*p.Width, PropertyValue(120.0));

    Compositor compositor;
    Framework ui(compositor);
    const ElementId page = ui.create(ElementKind::kPage);
    const ElementId id = ui.insert(button, page);
    check(id && button->isInTree() && ui.tree().uiElement(id) == button.get(),
          "insert hosts the same object");
    check(ui.tree().property(id, *ui.props().Content).asString() == "Save" &&
              ui.tree().property(id, *ui.props().Width).asDouble() == 120.0,
          "values set before insertion are the tree's values (same property ids)");
    check(ui.insert(button) == 0, "an element cannot be inserted twice");
    ui.update(400, 300);
    check(ui.bounds(id).width == 120, "the inserted element lays out with its own values");

    // Removing keeps the object and lets it move to another root.
    ui.tree().remove(id);
    check(!button->isInTree() && button->getValue(*p.Width).asDouble() == 120.0,
          "a removed element keeps its values and leaves the tree");
    Compositor otherCompositor;
    Framework other(otherCompositor);
    check(other.insert(button) != 0, "a removed element can be hosted by another root");
}

void layoutOverrides()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    const ElementId grid = ui.create(ElementKind::kGrid);
    ui.set(grid, p.ColumnDefinitions,
           GridLengths{{GridLength::Unit::kPixel, 100}, {GridLength::Unit::kStar, 1}});
    const ElementId left = ui.create(ElementKind::kBorder, grid);
    const ElementId right = ui.create(ElementKind::kBorder, grid);
    ui.set(right, p.GridColumn, i64{1});
    ui.update(400, 200);
    check(ui.bounds(left).width == 100 && ui.bounds(right).x == 100 && ui.bounds(right).width == 300,
          "Grid's arrange override places pixel and star columns");

    // Incremental invalidation: a property change dirties only what it affects.
    ui.update(400, 200);
    ui.set(right, p.Opacity, 0.5);
    check(!ui.tree().measureDirty(grid) && ui.tree().paintDirty(right),
          "a paint property does not re-measure");
    ui.set(right, p.Margin, Thickness{4, 4, 4, 4});
    check(ui.tree().measureDirty(grid), "a measure property invalidates the ancestors' layout");
}

void routedEvents()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto& events = ui.events();
    const ElementId page = ui.create(ElementKind::kPage);
    const ElementId stack = ui.create(ElementKind::kStackPanel, page);
    const ElementId text = ui.create(ElementKind::kTextBlock, stack);
    ui.set(text, ui.props().Text, std::string("label"));
    const ElementId button = ui.create(ElementKind::kButton, stack);
    ui.set(button, ui.props().Content, std::string("Go"));
    ui.update(300, 200);

    std::vector<std::string> log;
    u64 originalSource = 0;
    ui.addHandler(page, events.PointerPressed,
                  [&](u64, RoutedEventArgs& a) { log.push_back("page"), originalSource = a.originalSource; });
    ui.addHandler(stack, events.PointerPressed, [&](u64, RoutedEventArgs&) { log.push_back("stack"); });

    // A non-interactive element: the event bubbles unhandled to the root.
    const Box t = ui.bounds(text);
    ui.pointerDown(t.x + 2, t.y + 2);
    ui.pointerUp(t.x + 2, t.y + 2);
    check(log == std::vector<std::string>{"stack", "page"} && originalSource == text,
          "PointerPressed bubbles from the original source to the root");

    // A Button handles the press (class handling): ancestors see it only with handledEventsToo.
    log.clear();
    bool seenHandled = false;
    ui.addHandler(
        page, events.PointerPressed, [&](u64, RoutedEventArgs& a) { seenHandled = a.handled; }, true);
    const Box b = ui.bounds(button);
    ui.pointerDown(b.x + 2, b.y + 2);
    ui.pointerUp(b.x + 2, b.y + 2);
    check(log.empty() && seenHandled,
          "a Button marks PointerPressed handled; handledEventsToo still sees it");

    // A handler marking Handled stops ordinary handlers above it.
    log.clear();
    const u64 stopper =
        ui.addHandler(stack, events.PointerPressed, [&](u64, RoutedEventArgs& a) { a.handled = true; });
    ui.pointerDown(t.x + 2, t.y + 2);
    ui.pointerUp(t.x + 2, t.y + 2);
    check(log == std::vector<std::string>{"stack"},
          "Handled stops delivery to handlers without handledEventsToo");
    check(ui.removeHandler(stack, stopper) && !ui.removeHandler(stack, stopper),
          "handler tokens remove once");

    // Focus events.
    const ElementId box = ui.create(ElementKind::kTextBox, stack);
    ui.update(300, 200);
    int got = 0, lost = 0;
    ui.addHandler(page, events.GotFocus, [&](u64, RoutedEventArgs& a) { got += a.originalSource == box; });
    ui.addHandler(page, events.LostFocus, [&](u64, RoutedEventArgs& a) { lost += a.originalSource == box; });
    const Box x = ui.bounds(box);
    ui.pointerDown(x.x + 2, x.y + 2);
    ui.pointerUp(x.x + 2, x.y + 2);
    int keys = 0;
    ui.addHandler(page, events.KeyDown, [&](u64, RoutedEventArgs& a) { keys += a.originalSource == box; });
    ui.key(Key::kEnd);
    ui.pointerDown(t.x + 2, t.y + 2); // a press outside interactive elements does not move focus off the box
    ui.update(300, 200);
    const Box b2 = ui.bounds(button);
    ui.pointerDown(b2.x + 2, b2.y + 2); // a Button press clears focus
    ui.pointerUp(b2.x + 2, b2.y + 2);
    check(got == 1 && lost == 1, "GotFocus and LostFocus bubble from the focused element");
    check(keys == 1, "KeyDown bubbles from the focused element");

    // The legacy bool handlers keep their meaning: returning true marks the event handled.
    int clicks = 0;
    ui.on(button, events.Click, [&] { ++clicks; });
    ui.pointerDown(b2.x + 2, b2.y + 2);
    ui.pointerUp(b2.x + 2, b2.y + 2);
    check(clicks == 1, "Click still reaches its handler");
}

void typeHierarchy()
{
    RuntimeTypeRegistry types;
    check(registerBuiltinTypes(types) == RegistrationResult::kOk &&
              registerControlTypes(types) == RegistrationResult::kOk,
          "control types register");
    check(types.isA("Velora.UI.CheckBox", "Velora.UI.ButtonBase") &&
              types.isA("Velora.UI.Page", "Velora.UI.UserControl") &&
              types.isA("Velora.UI.Slider", "Velora.UI.RangeBase") &&
              types.isA("Velora.UI.StackPanel", "Velora.UI.Panel") &&
              types.isA("Velora.UI.ListView", kControlClassName) &&
              types.isA("Velora.UI.TextBlock", kUIElementClassName),
          "isA follows UWP's control hierarchy");
    check(!types.isA("Velora.UI.TextBlock", kControlClassName) &&
              !types.isA("Velora.UI.Border", "Velora.UI.Panel"),
          "TextBlock is not a Control and Border is not a Panel");
    // Every schema type is registered and maps from an element's className().
    for (usize i = 0; i <= static_cast<usize>(ElementKind::kCanvas); ++i)
    {
        const auto element = createElementObject(static_cast<ElementKind>(i));
        if (!types.find(element->className()))
            check(false, "every built-in element type is registered");
    }
}
} // namespace

int main()
{
    typedObjects();
    objectFirst();
    layoutOverrides();
    routedEvents();
    typeHierarchy();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
