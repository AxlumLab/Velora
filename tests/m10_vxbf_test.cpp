#include <velora/globals.hpp>
// R12.1: the M10 resources/styles/templates/visual-states acceptance, unchanged, through the .vxbf
// pipeline (velora_xaml_load + XamlLoader) instead of the legacy generated construction code. The
// page and the WinUI 2.3 dictionaries are the same files as the legacy m10 test's.
#include "StylesPage.g.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <velora/composition/Compositor.hpp>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const std::string& name)
{
    std::printf("m10 %s: %s\n", name.c_str(), passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

velora::i64 color(const Framework& ui, ElementId e, const PropertyDescriptor* p)
{
    const auto& v = ui.tree().property(e, *p);
    return v.isInt() ? v.asInt() : -1;
}
double number(const Framework& ui, ElementId e, const PropertyDescriptor* p)
{
    const auto& v = ui.tree().property(e, *p);
    return v.isDouble() ? v.asDouble() : -1;
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    M10::StylesPage controller;
    controller.toggleTheme = [&] { ui.setTheme(ui.theme() == Theme::kLight ? Theme::kDark : Theme::kLight); };
    M10::StylesPage_component::Names names;
    const auto load = M10::StylesPage_component::initializeComponent(ui, controller, names);
    if (!load.ok)
    {
        std::printf("m10 load: %s\n", load.error.c_str());
        return 1;
    }
    const ElementId root = ui.tree().element(*load.root);
    const auto id = [&](const std::shared_ptr<UIElement>& e) { return ui.tree().element(*e); };
    const struct
    {
        ElementId DefaultButton, PillA, PillB, ThemeButton, Input, Title, Subtitle, Card, CardText;
    } n{id(names.DefaultButton), id(names.PillA),    id(names.PillB), id(names.ThemeButton), id(names.Input),
        id(names.Title),         id(names.Subtitle), id(names.Card),  id(names.CardText)};
    ui.update(900, 500);
    const ElementId presenter = ui.templateChild(n.DefaultButton, "ContentPresenter");
    const ElementId chromeA = ui.templateChild(n.PillA, "Chrome"),
                    chromeB = ui.templateChild(n.PillB, "Chrome");
    const ElementId labelA = ui.templateChild(n.PillA, "Label");

    // ---- WinUI 2.3 values, Light -------------------------------------------------------
    check(presenter != 0 && color(ui, presenter, p.Background) == 0x33000000,
          "ButtonBackground = SystemControlBackgroundBaseLowBrush (#33000000)");
    check(color(ui, presenter, p.Foreground) == 0xFF000000,
          "ButtonForeground = SystemControlForegroundBaseHighBrush (#FF000000)");
    check(color(ui, presenter, p.BorderBrush) == 0x00FFFFFF,
          "ButtonBorderBrush = SystemControlForegroundTransparentBrush");
    check(number(ui, presenter, p.CornerRadius) == 2, "CornerRadius = ControlCornerRadius (2,2,2,2)");
    check(ui.tree().property(presenter, *p.Padding).asThickness() == Thickness{8, 5, 8, 6},
          "Padding = ButtonPadding (8,5,8,6)");
    check(ui.tree().property(presenter, *p.Content).asString() == "WinUI default",
          "ContentPresenter shows the button's Content");
    check(color(ui, n.Input, p.BorderBrush) == 0x99000000 &&
              ui.tree().property(n.Input, *p.Padding).asThickness() == Thickness{10, 5, 6, 6} &&
              number(ui, n.Input, p.MinHeight) == 32,
          "TextBox implicit style: TextControlBorderBrush, padding, min height");
    check(number(ui, n.Title, p.FontSize) == 26 && number(ui, n.Subtitle, p.FontSize) == 15 &&
              color(ui, n.Subtitle, p.Foreground) == 0xFF000000,
          "BasedOn: the derived style overrides FontSize and inherits Foreground");

    // ---- Templates: independent instances, Click keeps working ---------------------------
    std::set<ElementId> ids{presenter, chromeA, chromeB, labelA, ui.templateChild(n.PillB, "Label")};
    check(chromeA && chromeB && ids.size() == 5, "every templated button has its own template elements");
    check(controller.applied.size() == 4, "OnApplyTemplate runs for each templated button");
    const auto click = [&](ElementId e)
    {
        const Box b = ui.bounds(e);
        ui.pointerMove(b.x + 6, b.y + b.height / 2);
        ui.pointerDown(b.x + 6, b.y + b.height / 2);
        ui.pointerUp(b.x + 6, b.y + b.height / 2);
        ui.pointerMove(0, 0);
    };
    click(n.PillA);
    click(n.PillA);
    click(n.PillB);
    click(n.DefaultButton);
    check(controller.pillAClicks == 2 && controller.pillBClicks == 1 && controller.defaultClicks == 1,
          "Click works through the template visuals");
    check(ui.hitTest(ui.bounds(n.PillA).x + 6, ui.bounds(n.PillA).y + 4) != n.PillA,
          "the pointer hits the template, not the button itself");

    // ---- Visual states -------------------------------------------------------------------
    ui.advance(0);
    const Box db = ui.bounds(n.DefaultButton);
    ui.pointerMove(db.x + 4, db.y + 4);
    check(ui.currentState(n.DefaultButton, "CommonStates") == "PointerOver" &&
              color(ui, presenter, p.Background) == 0x1A000000,
          "PointerOver: ButtonBackgroundPointerOver (SystemBaseHighColor at Opacity 0.1)");
    ui.pointerDown(db.x + 4, db.y + 4);
    check(ui.currentState(n.DefaultButton, "CommonStates") == "Pressed" &&
              color(ui, presenter, p.Background) == 0x66000000,
          "Pressed: ButtonBackgroundPressed");
    ui.pointerUp(db.x + 4, db.y + 4);
    ui.pointerMove(0, 0);
    check(ui.currentState(n.DefaultButton, "CommonStates") == "Normal" &&
              color(ui, presenter, p.Background) == 0x33000000,
          "Normal restores the template value");
    ui.set(n.DefaultButton, p.IsEnabled, PropertyValue(false));
    check(ui.currentState(n.DefaultButton, "CommonStates") == "Disabled" &&
              color(ui, presenter, p.Foreground) == 0x66000000,
          "Disabled: ButtonForegroundDisabled");
    ui.set(n.DefaultButton, p.IsEnabled, PropertyValue(true));

    // Eased color animation on hover, only on the hovered instance.
    const auto pillColor = color(ui, chromeA, p.Background);
    const Box pa = ui.bounds(n.PillA);
    ui.advance(10.0);
    ui.pointerMove(pa.x + 6, pa.y + pa.height / 2);
    check(ui.animating() && color(ui, chromeA, p.Background) == pillColor,
          "PointerOver starts the storyboard at the current value");
    ui.advance(10.1);
    const auto mid = color(ui, chromeA, p.Background);
    check(mid != pillColor && mid != 0xFF005A9E, "halfway the color is between the two");
    // CubicEase EaseOut at t = 0.5 is 0.875 of the way: check the blue channel.
    const double expectedBlue = 0xD7 + (0x9E - 0xD7) * 0.875;
    check(std::abs(static_cast<double>(mid & 0xFF) - expectedBlue) <= 1.0,
          "the easing curve is CubicEase EaseOut");
    ui.advance(10.25);
    check(!ui.animating() && color(ui, chromeA, p.Background) == 0xFF005A9E,
          "the animation ends on its To value and stops (bounded)");
    check(color(ui, chromeB, p.Background) == pillColor &&
              ui.currentState(n.PillB, "CommonStates") == "Normal",
          "the other instance is untouched");
    ui.pointerDown(pa.x + 6, pa.y + pa.height / 2);
    ui.advance(10.4);
    check(color(ui, labelA, p.Foreground) == 0xFFFFE680 && number(ui, chromeA, p.Opacity) == 0.7,
          "Pressed: setter and DoubleAnimation");
    ui.pointerUp(pa.x + 6, pa.y + pa.height / 2);
    check(color(ui, labelA, p.Foreground) == 0xFFFFFFFF, "leaving Pressed restores the local value");

    // ---- Theme switch: immediate, including active states --------------------------------
    ui.advance(11.0);
    click(n.ThemeButton); // the controller toggles the theme
    ui.pointerMove(pa.x + 6, pa.y + pa.height / 2);
    ui.advance(12.0);
    check(ui.theme() == Theme::kDark, "Toggle theme button switches to Dark");
    check(color(ui, presenter, p.Background) == 0x33FFFFFF &&
              color(ui, presenter, p.Foreground) == 0xFFFFFFFF,
          "WinUI Button resources in Dark");
    check(color(ui, root, p.Background) == 0xFF202020 && color(ui, n.Card, p.Background) == 0xFF2D2D2D &&
              color(ui, n.CardText, p.Foreground) == 0xFFFFFFFF,
          "page ThemeDictionaries switch immediately");
    check(color(ui, n.Input, p.BorderBrush) == 0x99FFFFFF &&
              color(ui, n.Subtitle, p.Foreground) == 0xFFFFFFFF,
          "style ThemeResource setters switch");
    check(color(ui, chromeA, p.Background) == 0xFF429CE3,
          "an active animated state switches to the Dark keyframe value");
    ui.pointerMove(db.x + 4, db.y + 4);
    check(color(ui, presenter, p.Background) == 0x1AFFFFFF,
          "PointerOver in Dark: SystemBaseHighColor at Opacity 0.1");
    ui.setTheme(Theme::kLight);
    check(color(ui, presenter, p.Background) == 0x1A000000 && color(ui, root, p.Background) == 0xFFF3F3F3,
          "and back to Light");

    // ---- Lifetime ------------------------------------------------------------------------
    ui.tree().remove(root);
    check(ui.tree().node_count() == 0 && ui.templateChild(n.PillA, "Chrome") == 0 && !ui.animating(),
          "destroying the view removes templates and states");
    std::printf("m10 exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
