#include <velora/globals.hpp>
// Built-in UI framework: schema defaults, layout, hit testing, input and painting.
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>
#include <velora/composition/Compositor.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/runtime/PropertyMetadata.hpp>
#include <velora/runtime/SchemaValue.hpp>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("framework %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

bool near(float a, float b)
{
    return std::fabs(a - b) < 0.01f;
}

// 10 px per character keeps expected sizes easy to compute.
float fixedWidth(std::string_view text, float)
{
    return static_cast<float>(text.size()) * 10.0f;
}

} // namespace

int main()
{
    for (const char* type : {"i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "velora::i64",
                             "velora::u32", "usize", "isize"})
    {
        bool parsed = false;
        try
        {
            parsed = parseDefault(type, "42").asInt() == 42;
        }
        catch (const std::invalid_argument&)
        {
        }
        check(parsed, "project integer alias parsed as a property default");
    }
    for (const char* type : {"f32", "f64", "velora::f32", "velora::f64"})
    {
        bool parsed = false;
        try
        {
            parsed = parseDefault(type, "1.5").asDouble() == 1.5;
        }
        catch (const std::invalid_argument&)
        {
        }
        check(parsed, "project floating alias parsed as a property default");
    }

    // Values shared with the compiler.
    check(parseColor("#FF0078D4") == 0xFF0078D4 && parseColor("#08F") == 0xFF0088FF &&
              parseColor("White") == 0xFFFFFFFF && !parseColor("#12345"),
          "colors");
    check(parseThickness("1,2") == Thickness{1, 2, 1, 2} &&
              parseThickness("1 2 3 4") == Thickness{1, 2, 3, 4} && !parseThickness("1,2,3"),
          "thickness");
    const auto gridLengths =
        std::get<PropertyValue>(parseValue(*findSchemaProperty("RowDefinitions"), "Auto,2*,40"));
    check(gridLengths.asGridLengths() == GridLengths{{GridLength::Unit::kAuto, 0},
                                                     {GridLength::Unit::kStar, 2},
                                                     {GridLength::Unit::kPixel, 40}},
          "grid lengths");
    check(
        std::holds_alternative<std::string>(parseValue(*findSchemaProperty("HorizontalAlignment"), "Middle")),
        "enum rejected");
    check(std::get<PropertyValue>(parseValue(*findSchemaProperty("Visibility"), "Collapsed")) ==
              PropertyValue(false),
          "visibility");
    check(std::get<PropertyValue>(parseValue(*findSchemaProperty("Width"), "Auto")).empty(),
          "Auto width is unset");

    Compositor compositor;
    Framework ui(compositor, fixedWidth);
    const auto& p = ui.props();
    const ElementId page = ui.create(ElementKind::kPage);
    const ElementId stack = ui.create(ElementKind::kStackPanel, page);
    ui.set(stack, p.Margin, Thickness{20, 10, 20, 10});
    ui.set(stack, p.Spacing, 8.0);
    const ElementId title = ui.create(ElementKind::kTextBlock, stack);
    ui.set(title, p.Text, "Hello");
    ui.set(title, p.FontSize, 30.0);
    const ElementId box = ui.create(ElementKind::kTextBox, stack);
    ui.set(box, p.PlaceholderText, "Name");
    const ElementId button = ui.create(ElementKind::kButton, stack);
    ui.set(button, p.Content, "Save");
    check(ui.tree().property(button, *p.HorizontalAlignment).asString() == "Left" &&
              ui.tree().propertySource(button, *p.Padding) == PropertySource::kDefaultStyle,
          "per-type defaults in the DefaultStyle layer, below application styles");

    ui.update(400, 300);
    const Box t = ui.bounds(title), b = ui.bounds(box), btn = ui.bounds(button);
    check(near(t.x, 20) && near(t.y, 10) && near(t.width, 360) && near(t.height, lineHeight(30)),
          "text block stretched below the margin");
    check(near(b.y, t.y + t.height + 8) && near(b.width, 360) && near(b.height, lineHeight(14) + 13),
          "text box stacked with spacing");
    // Button: Left aligned, content width 40 + padding 22 + border 2.
    check(near(btn.x, 20) && near(btn.width, 64) && near(btn.y, b.y + b.height + 8),
          "button sized to content");
    check(ui.hitTest(btn.x + 5, btn.y + 5) == button && ui.hitTest(390, 290) == page, "hit test");

    int clicks = 0;
    ui.on(button, ui.events().Click, [&] { ++clicks; });
    ui.pointerDown(btn.x + 5, btn.y + 5);
    check(ui.tree().property(button, *p.IsPressed).asBool(), "pressed state");
    ui.pointerUp(btn.x + 6, btn.y + 6);
    ui.pointerDown(btn.x + 5, btn.y + 5);
    ui.pointerUp(390, 290); // released outside: no click
    check(clicks == 1 && !ui.tree().property(button, *p.IsPressed).asBool(),
          "click only when released inside");

    int changes = 0;
    ui.on(box, ui.events().TextChanged, [&] { ++changes; });
    ui.pointerDown(b.x + 5, b.y + 5);
    ui.textInput("ab");
    ui.textInput("\xE4\xB8\xAD");
    ui.key(Key::kBackspace);
    check(ui.focused() == box && ui.tree().property(box, *p.Text).asString() == "ab" && changes == 3,
          "text input and backspace");
    ui.pointerDown(390, 290);
    check(ui.focused() == 0, "clicking elsewhere clears focus");

    // Grid: Auto row from content, star columns share the rest 1:3.
    const ElementId grid = ui.create(ElementKind::kGrid);
    ui.set(grid, p.RowDefinitions, GridLengths{{GridLength::Unit::kAuto, 0}, {GridLength::Unit::kStar, 1}});
    ui.set(grid, p.ColumnDefinitions,
           GridLengths{{GridLength::Unit::kStar, 1}, {GridLength::Unit::kStar, 3}});
    const ElementId header = ui.create(ElementKind::kTextBlock, grid);
    ui.set(header, p.Text, "Header");
    ui.set(header, p.GridColumnSpan, velora::i64{2});
    const ElementId cell = ui.create(ElementKind::kBorder, grid);
    ui.set(cell, p.GridRow, velora::i64{1});
    ui.set(cell, p.GridColumn, velora::i64{1});
    ui.update(400, 300);
    const Box h = ui.bounds(header), c = ui.bounds(cell);
    check(near(h.width, 400) && near(c.x, 100) && near(c.width, 300) && near(c.y, lineHeight(14)) &&
              near(c.height, 300 - lineHeight(14)),
          "grid auto row and star columns");

    // Layout runs only when something changed.
    const auto frame = compositor.needsFrame();
    compositor.snapshot();
    ui.update(400, 300);
    check(frame && !compositor.needsFrame(), "no work when nothing changed");
    ui.set(title, p.Text, "Hello, world");
    ui.update(400, 300);
    check(compositor.needsFrame(), "text change repaints");

    // The window background follows the theme (WinUI ApplicationPageBackgroundThemeBrush).
    {
        Compositor c;
        Framework themed(c);
        const auto clear = [&]
        {
            const auto scene = c.snapshot();
            const auto* command = std::get_if<ClearCommand>(&scene.displayList->commands().front());
            return command ? command->color : Color{};
        };
        const Color light = clear();
        themed.setTheme(Theme::kDark);
        const Color dark = clear();
        check(light.r == 1 && light.g == 1 && light.b == 1 && light.a == 1 && dark.r == 0 && dark.g == 0 &&
                  dark.b == 0 && dark.a == 1,
              "window background is white in Light and black in Dark");
    }

    std::printf("framework exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
