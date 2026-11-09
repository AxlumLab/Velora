// ScrollViewer: layout of the scrollable axes, ChangeView clamping, clipping and hit
// testing, wheel chaining between nested scrollers, the panning-indicator overlay and
// object content in the retained tree.
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <velora/globals.hpp>
#include <velora/composition/Compositor.hpp>
#include <velora/runtime/Framework.hpp>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("scrollviewer %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

bool near(double a, double b)
{
    return std::fabs(a - b) < 0.01;
}

struct Fixture
{
    Compositor compositor;
    Framework ui{compositor};
    const Framework::Properties& p = ui.props();

    double get(ElementId e, const PropertyDescriptor* d) const
    {
        return ui.tree().property(e, *d).asDouble();
    }
    // A Height-tall block of the given width (NaN: stretch).
    ElementId block(ElementId parent, double height, double width = std::nan(""))
    {
        const auto e = ui.create(ElementKind::kBorder, parent);
        ui.set(e, p.Height, PropertyValue(height));
        if (!std::isnan(width))
            ui.set(e, p.Width, PropertyValue(width));
        return e;
    }
};

void verticalScrolling()
{
    Fixture f;
    auto& ui = f.ui;
    const auto page = ui.create(ElementKind::kPage);
    const auto viewer = ui.create(ElementKind::kScrollViewer, page);
    ui.set(viewer, f.p.Width, PropertyValue(200.0));
    ui.set(viewer, f.p.Height, PropertyValue(100.0));
    ui.set(viewer, f.p.HorizontalAlignment, PropertyValue(std::string("Left")));
    ui.set(viewer, f.p.VerticalAlignment, PropertyValue(std::string("Top")));
    const auto stack = ui.create(ElementKind::kStackPanel, viewer);
    ElementId rows[6]{};
    for (auto& row : rows)
        row = f.block(stack, 50);
    ui.changeView(viewer, std::nullopt, 1000.0); // before any layout: clamped by the first one
    ui.update(400, 400);
    check(near(f.get(viewer, f.p.VerticalOffset), 200),
          "ChangeView before the first layout is kept and clamped");
    ui.changeView(viewer, std::nullopt, 0.0);
    ui.update(400, 400);

    check(near(f.get(viewer, f.p.ExtentHeight), 300) && near(f.get(viewer, f.p.ViewportHeight), 100) &&
              near(f.get(viewer, f.p.ExtentWidth), 200) && near(f.get(viewer, f.p.ViewportWidth), 200),
          "vertical axis measures unconstrained; disabled horizontal axis fits the viewport");
    check(near(ui.bounds(stack).width, 200), "content stretches to the viewport width");
    check(ui.tree().property(viewer, *ui.props().VerticalScrollBarVisibility).asString() == "Visible" &&
              ui.tree().property(viewer, *ui.props().HorizontalScrollBarVisibility).asString() == "Disabled",
          "WinUI ScrollBarVisibility defaults");

    check(ui.changeView(viewer, std::nullopt, 150.0), "ChangeView on a ScrollViewer");
    ui.update(400, 400);
    check(near(f.get(viewer, f.p.VerticalOffset), 150) && near(ui.bounds(rows[3]).y, 0),
          "the vertical offset moves the content up");
    ui.changeView(viewer, 80.0, 1000.0);
    ui.update(400, 400);
    check(near(f.get(viewer, f.p.VerticalOffset), 200) && near(f.get(viewer, f.p.HorizontalOffset), 0),
          "offsets clamp to the scrollable range; a disabled axis stays at 0");
    check(!ui.changeView(stack, 0.0, 0.0), "ChangeView rejects non-ScrollViewer elements");

    check(ui.hitTest(50, 50) == rows[5] && ui.hitTest(50, 150) == page,
          "hit testing follows the offset and is clipped to the viewport");

    // Removing content shrinks the extent and pulls the offset back into range.
    ui.tree().remove(rows[5]);
    ui.tree().remove(rows[4]);
    ui.update(400, 400);
    check(near(f.get(viewer, f.p.ExtentHeight), 200) && near(f.get(viewer, f.p.VerticalOffset), 100),
          "a smaller extent clamps the stored offset");

    const auto visual = ui.bridge().visual(viewer);
    check(f.compositor.children(visual).size() == ui.tree().visualChildren(viewer).size() + 1,
          "the panning indicator is an overlay visual above the content");
}

void wheelChaining()
{
    Fixture f;
    auto& ui = f.ui;
    const auto page = ui.create(ElementKind::kPage);
    const auto outer = ui.create(ElementKind::kScrollViewer, page);
    ui.set(outer, f.p.Height, PropertyValue(300.0));
    ui.set(outer, f.p.VerticalAlignment, PropertyValue(std::string("Top")));
    const auto stack = ui.create(ElementKind::kStackPanel, outer);
    const auto inner = ui.create(ElementKind::kScrollViewer, stack);
    ui.set(inner, f.p.Height, PropertyValue(100.0));
    const auto innerStack = ui.create(ElementKind::kStackPanel, inner);
    for (int i = 0; i < 3; ++i)
        f.block(innerStack, 100);
    f.block(stack, 500);
    ui.update(400, 400);

    ui.wheel(10, 50, 120);
    ui.update(400, 400);
    check(near(f.get(inner, f.p.VerticalOffset), 120) && near(f.get(outer, f.p.VerticalOffset), 0),
          "the innermost scroller takes the wheel");
    ui.wheel(10, 50, 500);
    ui.update(400, 400);
    check(near(f.get(inner, f.p.VerticalOffset), 200), "the wheel clamps at the end of the range");
    ui.wheel(10, 50, 40);
    ui.update(400, 400);
    check(near(f.get(inner, f.p.VerticalOffset), 200) && near(f.get(outer, f.p.VerticalOffset), 40),
          "at its end the wheel chains to the outer scroller");
    ui.wheel(10, 10, -1000);
    ui.update(400, 400);
    check(near(f.get(inner, f.p.VerticalOffset), 0) && near(f.get(outer, f.p.VerticalOffset), 40),
          "scrolling back moves the inner scroller first");

    // A viewer that cannot scroll vertically sends the wheel to its horizontal axis.
    Fixture h;
    const auto hp = h.ui.create(ElementKind::kPage);
    const auto row = h.ui.create(ElementKind::kScrollViewer, hp);
    h.ui.set(row, h.p.Width, PropertyValue(100.0));
    h.ui.set(row, h.p.HorizontalAlignment, PropertyValue(std::string("Left")));
    h.ui.set(row, h.p.HorizontalScrollBarVisibility, PropertyValue(std::string("Auto")));
    h.ui.set(row, h.p.VerticalScrollBarVisibility, PropertyValue(std::string("Disabled")));
    h.block(row, 40, 450);
    h.ui.update(400, 400);
    h.ui.wheel(10, 10, 60);
    h.ui.update(400, 400);
    check(near(h.get(row, h.p.ExtentWidth), 450) && near(h.get(row, h.p.HorizontalOffset), 60),
          "a horizontal-only viewer scrolls horizontally with the wheel");
}

// A 200x100 viewer at (0,0) over 300 DIPs of rows, a Button in the first row.
struct Scroller : Fixture
{
    ElementId page = 0, viewer = 0, stack = 0, button = 0;
    Scroller()
    {
        page = ui.create(ElementKind::kPage);
        viewer = ui.create(ElementKind::kScrollViewer, page);
        ui.set(viewer, p.Width, PropertyValue(200.0));
        ui.set(viewer, p.Height, PropertyValue(100.0));
        ui.set(viewer, p.HorizontalAlignment, PropertyValue(std::string("Left")));
        ui.set(viewer, p.VerticalAlignment, PropertyValue(std::string("Top")));
        stack = ui.create(ElementKind::kStackPanel, viewer);
        button = ui.create(ElementKind::kButton, stack);
        ui.set(button, p.Content, PropertyValue(std::string("B")));
        ui.set(button, p.Height, PropertyValue(40.0));
        block(stack, 260);
        ui.update(400, 400);
    }
    double offset() const
    {
        return get(viewer, p.VerticalOffset);
    }
};

void keyboard()
{
    Scroller s;
    auto& ui = s.ui;
    ui.key(Key::kDown);
    check(near(s.offset(), 0), "keys do nothing without focus");
    ui.pointerDown(150, 80); // on the content, not the button
    ui.pointerUp(150, 80);
    check(ui.focused() == s.viewer, "pressing the content focuses the ScrollViewer");
    ui.key(Key::kDown);
    ui.update(400, 400);
    check(near(s.offset(), 16), "Down scrolls one line");
    ui.key(Key::kPageDown);
    ui.update(400, 400);
    check(near(s.offset(), 116), "PageDown scrolls one viewport");
    ui.key(Key::kEnd);
    ui.update(400, 400);
    check(near(s.offset(), 200), "End goes to the bottom");
    ui.key(Key::kHome);
    ui.update(400, 400);
    check(near(s.offset(), 0), "Home goes to the top");

    // A focused TextBox keeps its editing keys; scrolling keys reach the viewer.
    const auto box = ui.create(ElementKind::kTextBox, s.stack);
    ui.set(box, s.p.Text, PropertyValue(std::string("abc")));
    ui.update(400, 400);
    const Box b = ui.bounds(box);
    ui.changeView(s.viewer, std::nullopt, b.y + s.offset() - 20);
    ui.update(400, 400);
    const Box shown = ui.bounds(box);
    ui.pointerDown(shown.x + 5, shown.y + shown.height / 2);
    ui.pointerUp(shown.x + 5, shown.y + shown.height / 2);
    const double before = s.offset();
    ui.key(Key::kEnd);
    ui.update(400, 400);
    check(ui.focused() == box && near(s.offset(), before) &&
              ui.tree().property(box, *s.p.CaretPosition).asInt() == 3,
          "End in a TextBox moves the caret, not the viewer");
    ui.key(Key::kUp);
    ui.update(400, 400);
    check(near(s.offset(), before - 16), "Up from a TextBox scrolls the viewer");
}

void scrollBar()
{
    Scroller s;
    auto& ui = s.ui;
    auto parts = ui.scrollBar(s.viewer, true);
    check(parts.shown && !parts.expanded && near(parts.thumb.width, 3),
          "collapsed bar is a panning indicator");
    check(!ui.scrollBar(s.viewer, false).shown, "a disabled axis has no bar");
    ui.pointerMove(195, 50);
    parts = ui.scrollBar(s.viewer, true);
    check(parts.expanded && near(parts.bar.x, 188) && near(parts.decrease.height, 12) &&
              near(parts.thumb.width, 6),
          "the pointer over the bar expands it with arrows");
    ui.pointerDown(195, 95); // increase arrow
    ui.pointerUp(195, 95);
    ui.update(400, 400);
    check(near(s.offset(), 16), "the increase arrow scrolls one line");
    ui.pointerDown(195, 80); // track below the thumb (thumb spans about 12..41 + offset)
    ui.pointerUp(195, 80);
    ui.update(400, 400);
    check(near(s.offset(), 116), "the track pages towards the pointer");

    ui.changeView(s.viewer, std::nullopt, 0.0);
    ui.update(400, 400);
    parts = ui.scrollBar(s.viewer, true);
    const float grabY = parts.thumb.y + 5;
    ui.pointerDown(195, grabY);
    ui.pointerMove(150, grabY + (parts.track.height - parts.thumb.height) / 2); // half the free track
    ui.update(400, 400);
    check(near(s.offset(), 100), "dragging the thumb maps the track onto the scroll range");
    ui.pointerMove(150, 1000);
    ui.update(400, 400);
    check(near(s.offset(), 200), "dragging clamps at the end");
    ui.pointerUp(150, 1000);
    check(!ui.scrollBar(s.viewer, true).expanded, "the bar collapses after a drag ends away from it");
}

void viewChanged()
{
    Scroller s;
    auto& ui = s.ui;
    int raised = 0;
    ui.on(s.viewer, ui.events().ViewChanged, [&] { ++raised; });
    ui.update(400, 400);
    check(raised == 0, "no ViewChanged without movement");
    ui.changeView(s.viewer, std::nullopt, 50.0);
    ui.changeView(s.viewer, std::nullopt, 60.0);
    ui.update(400, 400);
    check(raised == 1, "one ViewChanged per layout that moved the view");
    ui.on(s.viewer, ui.events().ViewChanged, [&] { ui.tree().remove(s.page); });
    ui.changeView(s.viewer, std::nullopt, 0.0);
    ui.update(400, 400);
    check(!ui.tree().contains(s.viewer), "a ViewChanged handler may remove the viewer");
}

void contentAndOffsets()
{
    Fixture f;
    auto& ui = f.ui;
    const auto page = ui.create(ElementKind::kPage);
    const auto viewer = ui.create(ElementKind::kScrollViewer, page);
    check(ui.tree().is(viewer, ElementKind::kScrollViewer), "the factory creates a retained ScrollViewer");
    ui.set(viewer, f.p.Width, PropertyValue(100.0));
    check(near(f.get(viewer, f.p.Width), 100), "element property writes reach the retained node");

    const auto child = ui.createDetached(ElementKind::kBorder, page);
    ui.set(child, f.p.Height, PropertyValue(500.0));
    check(ui.setContentObject(viewer, child) && ui.contentObject(viewer) == child,
          "same-page object content becomes the scrolled child");
    ui.update(400, 300);
    check(near(f.get(viewer, f.p.ExtentHeight), 500) && near(f.get(viewer, f.p.ViewportHeight), 300),
          "extent and viewport report the last layout");
    check(!ui.changeView(viewer, std::nullopt, std::numeric_limits<f64>::quiet_NaN()) &&
              !ui.changeView(viewer, std::nullopt, std::numeric_limits<f64>::infinity()) &&
              !ui.changeView(viewer, std::numeric_limits<f64>::infinity(), 100.0) &&
              near(f.get(viewer, f.p.VerticalOffset), 0),
          "non-finite offsets are rejected without changing either axis");
    check(ui.changeView(viewer, std::nullopt, 1e9) && near(f.get(viewer, f.p.VerticalOffset), 200),
          "ChangeView clamps a finite request to the range");
    ui.update(400, 300);
    check(near(ui.bounds(child).y, -200), "the scrolled child is arranged at the offset");
    ui.tree().remove(page);
    check(!ui.tree().contains(viewer) && !ui.tree().contains(child) &&
              !ui.changeView(viewer, std::nullopt, 0.0),
          "removing a page retires the viewer and content and rejects later scrolling");
}

} // namespace

int main()
{
    verticalScrolling();
    wheelChaining();
    keyboard();
    scrollBar();
    viewChanged();
    contentAndOffsets();
    std::printf("scrollviewer exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
