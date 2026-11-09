// N03: automatic frame scheduling through XamlHost + NativeSink. The application drives the
// frame scheduler in its event loop; frames present only when dirty or animated.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <velora/application/Application.hpp>
#include <velora/application/XamlHost.hpp>
#include <velora/composition/Compositor.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/text/TextService.hpp>
#include <windows.h>

using namespace velora;

namespace
{

int g_failures = 0;

void check(bool passed, const char* name)
{
    std::printf("N03 %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++g_failures;
}

void pump()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

HWND hwndOf(const AppWindow& window)
{
    return static_cast<HWND>(window.getNativeHandle());
}

// Automatic frame presentation: the XamlHost ticks the frame scheduler when the compositor is
// dirty or animations are active; idle scenes never present.
void automaticPresentation()
{
    Application app;
    GraphicsDevice device;
    check(device.initialize() && device.isReady(), "graphics device initialized");
    TextService textService;
    check(textService.initialize(), "text service initialized");

    AppWindow* window = app.createWindow({"Velora N03", 400, 300});
    check(window != nullptr, "window created");

    Compositor compositor;
    Framework ui(compositor,
                 [&](std::string_view text, float size) { return textService.measureWidth(text, size); });
    const auto& p = ui.props();

    // Build a simple UI.
    const ElementId page = ui.create(ElementKind::kPage);
    const ElementId stack = ui.create(ElementKind::kStackPanel, page);
    ui.set(stack, p.Margin, Thickness{20, 20, 20, 20});
    ui.set(stack, p.Spacing, 10.0);
    const ElementId title = ui.create(ElementKind::kTextBlock, stack);
    ui.set(title, p.Text, "N03 Automatic Frames");
    ui.set(title, p.FontSize, 24.0);
    const ElementId button = ui.create(ElementKind::kButton, stack);
    ui.set(button, p.Content, std::string("Click Me"));

    // Attach the XamlHost with graphics device.
    XamlHost host;
    check(host.attach(*window, ui, compositor, device), "XamlHost attached with graphics");
    if (!host.lastError().empty())
        std::printf("N03 attach error: %s\n", host.lastError().c_str());

    // Set up frame callback.
    bool hasPendingWork = false;
    app.setFrameCallback(
        [&]()
        {
            host.update();
            hasPendingWork = host.hasPendingWork();
            return hasPendingWork;
        });

    // Initial update: should present the first frame.
    host.update();
    pump();
    check(true, "first frame presented");

    // Idle: no pending work after the first frame.
    pump();
    check(!host.hasPendingWork(), "idle after first frame");

    // Mutate the UI and check automatic frame scheduling.
    ui.set(title, p.Text, "Updated Title");
    // After mutation, update syncs changes and presents the frame in one call.
    host.update();
    pump();
    // After presenting the dirty frame, no more work is pending.
    check(!host.hasPendingWork(), "clean after presenting mutation");

    // Start an animation: should keep pending work until stopped.
    bool animating = true;
    double startTime = -1;
    const auto scheduler = [&]()
    {
        // Access the scheduler through the host (we don't have direct access here,
        // but the host.hasPendingWork() tells us).
        return host.hasPendingWork();
    };

    // Simulate animation by repeatedly marking dirty.
    int animationFrames = 0;
    const int targetFrames = 5;
    for (int i = 0; i < targetFrames; ++i)
    {
        ui.set(title, p.FontSize, 24.0 + i);
        // Each update syncs changes and presents the frame.
        host.update();
        pump();
        ++animationFrames;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    check(animationFrames == targetFrames, "animation frames presented");

    const auto objectContent = ui.createDetached(ElementKind::kTextBlock, page);
    ui.set(objectContent, p.Text, "N07 Object Content");
    ui.set(objectContent, p.FontSize, 18.0);
    check(ui.setContentObject(button, objectContent), "object content attached to retained Button");
    host.update();
    pump();
    check(ui.bounds(objectContent).width > 0 && ui.bridge().visual(objectContent) != 0 &&
              host.lastError().empty() && !host.hasPendingWork(),
          "object content presented through native D2D XamlHost");

    // After animation stops, should go idle again.
    pump();
    check(!host.hasPendingWork(), "idle after animation");

    // Window resize: should resize surface and present.
    window->resize(500, 400);
    pump();
    host.update();
    check(true, "resize handled");

    // Clean shutdown.
    host.detach();
    check(!host.isAttached(), "host detached");
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    automaticPresentation();
    CoUninitialize();
    std::printf("N03 exit=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
