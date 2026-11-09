// R04 acceptance on real windows: two windows of one application have independent roots —
// size, DPI scale, theme, focus, layout invalidation, Content lifecycle and Changed events — and
// closing one leaves the other's objects and rendering intact.
#include <cmath>
#include <cstdio>
#include <memory>
#include <velora/application/XamlRoot.hpp>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <velora/runtime/Framework.hpp>

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

struct Page
{
    ElementId page = 0, stack = 0, box = 0, button = 0;
};

Page buildPage(Framework& ui)
{
    Page p;
    p.page = ui.create(ElementKind::kPage);
    p.stack = ui.create(ElementKind::kStackPanel, p.page);
    p.box = ui.create(ElementKind::kTextBox, p.stack);
    p.button = ui.create(ElementKind::kButton, p.stack);
    ui.set(p.button, ui.props().Content, std::string("OK"));
    return p;
}

class RootWindow final : public XamlWindow
{
public:
    RootWindow(XamlApplication& app, f32 width, f32 height) : XamlWindow(app)
    {
        WindowOptions options;
        options.title = "Velora R04 root";
        options.width = width;
        options.height = height;
        options.isVisible = false;
        created = Create(options);
        if (created)
        {
            page = buildPage(GetFramework());
            SetRoot(page.page);
            Show();
            Update();
        }
    }
    bool created = false;
    Page page;
};

class RootTestApp final : public XamlApplication
{
public:
    void OnLaunched() override
    {
        run();
        application().exit(0);
    }

private:
    void run()
    {
        RootWindow a(*this, 400, 300);
        RootWindow b(*this, 250, 200);
        check(a.created && b.created, "two windows with their own roots");
        if (!a.created || !b.created)
            return;
        XamlRoot& rootA = a.GetXamlRoot();
        XamlRoot& rootB = b.GetXamlRoot();
        Framework& uiA = rootA.framework();
        Framework& uiB = rootB.framework();

        // Separate UI contexts: no shared Framework or composition tree.
        check(&uiA != &uiB && &rootA.compositor() != &rootB.compositor(),
              "roots share no Framework or Compositor");

        // Size and scale follow each root's own window. The client area is whole physical pixels, so a
        // DIP size is exact only to one physical pixel (250 DIPs at 175% is 437.5 px -> 438 px).
        const auto near = [](f32 dips, f32 expected, f32 scale) { return std::abs(dips - expected) <= 1.0f / scale; };
        check(near(rootA.width(), 400, rootA.rasterizationScale()) && near(rootA.height(), 300, rootA.rasterizationScale()) &&
                  near(rootB.width(), 250, rootB.rasterizationScale()) &&
                  near(rootB.height(), 200, rootB.rasterizationScale()),
              "each root has its window's DIP size");
        check(rootA.rasterizationScale() == a.GetAppWindow()->getDpiScale() &&
                  rootB.rasterizationScale() == b.GetAppWindow()->getDpiScale() &&
                  rootA.resourceContext().scale == rootA.rasterizationScale(),
              "each root has its window's rasterization scale and resource context");
        check(rootA.isHostVisible() && rootB.isHostVisible(), "shown windows are host-visible");

        // Changed events: only the root whose window changed.
        u32 changesA = 0, changesB = 0;
        rootA.addChanged([&](XamlRoot&, u32 c) { changesA |= c; });
        rootB.addChanged([&](XamlRoot&, u32 c) { changesB |= c; });
        a.GetAppWindow()->resize(420, 320);
        a.Update();
        b.Update();
        check((changesA & XamlRoot::kSizeChanged) && near(rootA.width(), 420, rootA.rasterizationScale()) &&
                  changesB == 0,
              "resizing one window changes only its root");
        check(uiB.bounds(b.page.page).width == rootB.width(), "the other root keeps its layout size");

        // Theme per root.
        changesA = changesB = 0;
        rootA.setTheme(Theme::kDark);
        check(rootA.theme() == Theme::kDark && rootB.theme() == Theme::kLight &&
                  rootA.resourceContext().theme == ResourceTheme::kDark &&
                  rootB.resourceContext().theme == ResourceTheme::kLight,
              "themes are per root");
        check((changesA & XamlRoot::kThemeChanged) && changesB == 0, "a theme change notifies its root only");

        // Focus and input per root.
        const Box box = uiA.bounds(a.page.box);
        uiA.pointerDown(box.x + 2, box.y + 2);
        uiA.pointerUp(box.x + 2, box.y + 2);
        check(uiA.focused() == a.page.box && uiB.focused() == 0, "focus is per root");

        // Layout invalidation does not cross roots.
        a.Update();
        b.Update();
        uiA.set(a.page.button, uiA.props().Width, 200.0);
        check(uiA.tree().measureDirty(a.page.page) && !uiB.tree().measureDirty(b.page.page),
              "invalidating one root leaves the other clean");
        a.Update();

        // UIElement.XamlRoot.
        const auto boxObjectA = uiA.tree().object(a.page.box);
        const auto boxObjectB = uiB.tree().object(b.page.box);
        check(XamlRoot::of(*boxObjectA) == &rootA && XamlRoot::of(*boxObjectB) == &rootB,
              "an element's XamlRoot is its own root");

        // Content lifecycle: replacing Content unmounts the old one without destroying it.
        const Page second = buildPage(uiA);
        uiA.set(a.page.box, uiA.props().Text, std::string("kept"));
        check(a.SetRoot(second.page) && rootA.content() == second.page, "Content can be replaced");
        check(uiA.tree().contains(a.page.page) && !uiA.isMounted(a.page.page) &&
                  XamlRoot::of(*boxObjectA) == nullptr,
              "the previous content is unmounted, not destroyed, and has no XamlRoot");
        check(uiA.focused() == 0, "focus inside unmounted content is released");
        check(uiA.tree().property(a.page.box, *uiA.props().Text).asString() == "kept",
              "unmounted content keeps its state");
        a.Update();
        check(uiA.hitTest(box.x + 2, box.y + 2) != a.page.box, "unmounted content is not hit-tested");
        check(a.SetRoot(a.page.page) && uiA.isMounted(a.page.page) && !uiA.isMounted(second.page) &&
                  XamlRoot::of(*boxObjectA) == &rootA,
              "the old content can be mounted again");

        // One root per element (C-3.6).
        check(!rootB.setContent(*uiA.tree().object(a.page.page)), "a root refuses another root's element");
        check(!rootA.setContent(a.page.stack), "a nested element cannot become Content");
        check(rootB.content() == b.page.page, "a refused Content leaves the root unchanged");

        // Closing one window leaves the other root working.
        auto keptA = uiA.tree().object(a.page.button);
        a.Close();
        a.Update();
        b.Update();
        check(a.IsClosed() && !b.IsClosed(), "one window closed, the other open");
        check(uiB.tree().contains(b.page.button) && uiB.bounds(b.page.button).width > 0,
              "the remaining root still lays out its content");
        check(keptA && keptA->getValue(*uiA.props().Width).asDouble() == 200.0,
              "objects of the closed root stay valid while held");
        check(rootB.isHostVisible(), "the remaining root is still host-visible");
    }
};
} // namespace

int main()
{
    RootTestApp app;
    const i32 code = app.Run();
    check(code == 0, "the application ran");
    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
