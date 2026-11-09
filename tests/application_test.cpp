// N01: native Application/AppWindow lifecycle and the XamlHost input bridge on real Win32
// windows. Input is posted to the windows' message queues and goes through the real window
// procedure; IME composition from a real keyboard is covered by platform_window_test --live-ime.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <velora/application/Application.hpp>
#include <velora/application/XamlHost.hpp>
#include <velora/composition/Compositor.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/runtime/Framework.hpp>
#include <windows.h>

using namespace velora;

namespace
{

int g_failures = 0;

void check(bool passed, const char* name)
{
    std::printf("N01 %s: %s\n", name, passed ? "PASS" : "FAIL");
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

bool closeTo(f32 a, f32 b, f32 tolerance = 1.0f)
{
    return std::fabs(a - b) <= tolerance;
}

const AppEvent* last(const std::vector<AppEvent>& events, AppEventKind kind)
{
    for (auto it = events.rbegin(); it != events.rend(); ++it)
        if (it->kind == kind)
            return &*it;
    return nullptr;
}

LPARAM pixels(const AppWindow& window, f32 x, f32 y)
{
    const f32 scale = window.getDpiScale();
    return MAKELPARAM(static_cast<int>(x * scale + 0.5f), static_cast<int>(y * scale + 0.5f));
}

double processCpuMs()
{
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto value = [](FILETIME t)
    { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return static_cast<double>(value(kernel) + value(user)) / 10000.0;
}

void windows(Application& app)
{
    AppWindow* a = app.createWindow({"Velora N01 A", 400, 300});
    AppWindow* b = app.createWindow({"Velora N01 B", 200, 150});
    check(a && b && hwndOf(*a) != hwndOf(*b) && app.getOpenWindowCount() == 2, "two native windows");
    const f32 scale = a->getDpiScale();
    check(closeTo(a->getWidth(), 400) && a->getPixelWidth() == static_cast<i32>(std::lround(400 * scale)),
          "client size in DIPs, framebuffer in pixels");

    std::vector<AppEvent> events;
    bool cancelNextClose = true;
    // The handler outlives this function only as long as the window is open: it closes below.
    a->setEventHandler(
        [&](AppEvent& event)
        {
            if (event.kind == AppEventKind::kCloseRequested && cancelNextClose)
                event.cancel = true;
            events.push_back(event);
        });

    // Input arrives in posting order, positions in DIPs.
    const HWND hwnd = hwndOf(*a);
    PostMessageW(hwnd, WM_MOUSEMOVE, 0, pixels(*a, 10, 20));
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pixels(*a, 11, 21));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, pixels(*a, 12, 22));
    PostMessageW(hwnd, WM_KEYDOWN, VK_LEFT, 0);
    PostMessageW(hwnd, WM_KEYUP, VK_LEFT, 0xC0000001);
    pump();
    std::vector<AppEventKind> kinds;
    for (const auto& e : events)
        if (e.kind >= AppEventKind::kKeyDown && e.kind <= AppEventKind::kWheel)
            kinds.push_back(e.kind);
    check(kinds == std::vector<AppEventKind>{AppEventKind::kPointerMove, AppEventKind::kPointerDown,
                                             AppEventKind::kPointerUp, AppEventKind::kKeyDown,
                                             AppEventKind::kKeyUp},
          "events keep their order");
    const AppEvent* down = last(events, AppEventKind::kPointerDown);
    check(down && down->code == 1 && closeTo(down->x, 11) && closeTo(down->y, 21),
          "pointer position in DIPs");

    a->resize(500, 350);
    pump();
    const AppEvent* resized = last(events, AppEventKind::kResized);
    check(resized && closeTo(resized->width, 500) &&
              resized->pixelWidth == static_cast<i32>(std::lround(500 * scale)) &&
              closeTo(a->getHeight(), 350),
          "resize reported in DIPs and pixels");

    ShowWindow(hwnd, SW_MINIMIZE);
    pump();
    check(a->isMinimized() && last(events, AppEventKind::kMinimized), "minimize");
    ShowWindow(hwnd, SW_RESTORE);
    pump();
    check(!a->isMinimized() && last(events, AppEventKind::kRestored) && closeTo(a->getWidth(), 500),
          "restore keeps the size");

    a->hide();
    pump();
    check(!a->isVisible() && last(events, AppEventKind::kHidden), "hide");
    a->show();
    pump();
    check(a->isVisible() && last(events, AppEventKind::kShown), "show");

    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    pump();
    check(!a->isClosed() && IsWindow(hwnd) && last(events, AppEventKind::kCloseRequested),
          "close request can be cancelled");
    cancelNextClose = false;
    a->requestClose();
    pump();
    check(a->isClosed() && !IsWindow(hwnd) && last(events, AppEventKind::kClosed) &&
              app.getOpenWindowCount() == 1,
          "close request accepted");

    check(!b->isClosed(), "other window unaffected");
}

void lifecycle()
{
    {
        // run() dispatches until the last window closes.
        Application app;
        bool launched = false;
        app.setLaunchHandler(
            [&](Application& self)
            {
                launched = true;
                AppWindow* w = self.createWindow({"Velora N01 run", 200, 120});
                PostMessageW(hwndOf(*w), WM_CLOSE, 0, 0);
            });
        check(app.run() == 0 && launched && app.getOpenWindowCount() == 0,
              "run ends when the last window closes");
    }
    {
        // exit() from another thread while idle; the idle wait costs almost no CPU.
        Application app;
        app.setShutdownMode(ShutdownMode::kExplicit);
        double idleCpu = -1;
        std::chrono::steady_clock::duration exitLatency{};
        std::chrono::steady_clock::time_point exitCalled;
        app.setLaunchHandler(
            [&](Application& self)
            {
                (void)self.createWindow({"Velora N01 idle", 200, 120});
                std::thread(
                    [&]
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        const double before = processCpuMs();
                        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                        idleCpu = processCpuMs() - before;
                        exitCalled = std::chrono::steady_clock::now();
                        app.exit(7);
                    })
                    .detach();
            });
        const int code = app.run();
        exitLatency = std::chrono::steady_clock::now() - exitCalled;
        std::printf("N01 idle: %.1f ms CPU over 1500 ms, exit latency %.1f ms\n", idleCpu,
                    std::chrono::duration<double, std::milli>(exitLatency).count());
        check(code == 7 && exitLatency < std::chrono::milliseconds(200),
              "exit from another thread wakes run()");
        check(idleCpu >= 0 && idleCpu < 30.0, "idle event wait uses almost no CPU");
        int offThread = 0;
        std::thread([&] { offThread = app.run(); }).join();
        check(offThread == -1, "run() refuses other threads");
    }
    {
        // Destroying the application destroys its windows, reporting kClosed while it still exists.
        HWND hwnd = nullptr;
        bool closed = false;
        {
            Application app;
            AppWindow* w = app.createWindow({"Velora N01 shutdown", 200, 120});
            hwnd = hwndOf(*w);
            w->setEventHandler(
                [&](AppEvent& event)
                {
                    if (event.kind == AppEventKind::kClosed)
                        closed = true;
                });
        }
        check(closed && !IsWindow(hwnd), "shutdown closes windows first");
    }
}

void host(Application& app)
{
    AppWindow* window = app.createWindow({"Velora N01 XamlHost", 400, 300});
    GraphicsDevice device;
    check(device.initialize() && device.isReady(), "graphics device initialized for host test");
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    const ElementId page = ui.create(ElementKind::kPage);
    const ElementId stack = ui.create(ElementKind::kStackPanel, page);
    ui.set(stack, p.Spacing, 8.0);
    ui.set(stack, p.Margin, Thickness{10, 10, 10, 10});
    const ElementId button = ui.create(ElementKind::kButton, stack);
    ui.set(button, p.Content, std::string("Go"));
    const ElementId box = ui.create(ElementKind::kTextBox, stack);
    ui.set(box, p.Width, 200.0);
    int clicks = 0;
    ui.on(button, ui.events().Click, [&] { ++clicks; });

    XamlHost xaml;
    std::vector<AppEventKind> after;
    xaml.setEventHandler([&](AppEvent& event) { after.push_back(event.kind); });
    check(xaml.attach(*window, ui, compositor, device) && xaml.isAttached(), "attach the retained UI");
    if (!xaml.lastError().empty())
        std::printf("N01 host attach error: %s\n", xaml.lastError().c_str());
    xaml.update();
    const HWND hwnd = hwndOf(*window);
    const auto center = [&](ElementId e)
    {
        const Box b = ui.bounds(e);
        return pixels(*window, b.x + b.width / 2, b.y + b.height / 2);
    };

    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, center(button));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, center(button));
    pump();
    check(clicks == 1 && !after.empty(), "native click reaches the Button; app handler runs after the host");

    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, center(box));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, center(box));
    pump();
    check(ui.focused() == box && window->isTextInputActive(), "TextBox focus turns IME text input on");
    PostMessageW(hwnd, WM_CHAR, 'h', 0);
    PostMessageW(hwnd, WM_CHAR, 'i', 0);
    PostMessageW(hwnd, WM_KEYDOWN, VK_BACK, 0);
    pump();
    const auto text = [&] { return ui.tree().property(box, *p.Text).asString(); };
    check(text() == "h", "WM_CHAR commits and VK_BACK edits");

    // IME composition as the window reports it (TSF/IMM): preedit is shown, not committed.
    AppEvent preedit;
    preedit.kind = AppEventKind::kTextPreedit;
    preedit.text = "\xE4\xBD\xA0h"; // "你h", caret after the first code point (3 bytes)
    preedit.textCursor = 3;
    xaml.dispatch(preedit);
    check(text() == "h" && ui.tree().property(box, *p.PreeditText).asString() == preedit.text &&
              ui.tree().property(box, *p.PreeditCursor).asInt() == 1,
          "preedit shown at the caret (byte caret -> code points), text unchanged");
    AppEvent commit;
    commit.kind = AppEventKind::kTextCommit;
    commit.text = "\xE4\xBD\xA0";
    xaml.dispatch(commit);
    check(text() == "h\xE4\xBD\xA0" && ui.tree().property(box, *p.PreeditText).asString().empty(),
          "commit ends the composition");

    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, pixels(*window, 390, 290));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, pixels(*window, 390, 290));
    pump();
    check(ui.focused() == 0 && !window->isTextInputActive(), "leaving the TextBox turns IME text input off");

    // DPI change: composition gets the new scale, layout keeps its DIP size.
    const f32 width = window->getWidth();
    const f32 stackWidth = ui.bounds(stack).width;
    RECT frame{};
    GetWindowRect(hwnd, &frame);
    const UINT dpi = static_cast<UINT>(std::lround(window->getDpiScale() * 96 * 1.5f));
    // As Windows does: ask the window for its size at the new DPI, then suggest that rectangle.
    SIZE scaled{};
    SendMessageW(hwnd, WM_GETDPISCALEDSIZE, dpi, reinterpret_cast<LPARAM>(&scaled));
    RECT suggested{frame.left, frame.top, frame.left + scaled.cx, frame.top + scaled.cy};
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
    pump();
    xaml.update();
    const SceneSnapshot scene = compositor.snapshot();
    std::printf("N01 dpi: scale %.3f -> %.3f, window %.1f -> %.1f DIPs, scene %.1f x %.1f @%.3f, stack %.1f "
                "-> %.1f\n",
                static_cast<double>(dpi) / 96, window->getDpiScale(), width, window->getWidth(), scene.width,
                scene.height, scene.dpi, stackWidth, ui.bounds(stack).width);
    check(closeTo(window->getDpiScale(), static_cast<f32>(dpi) / 96, 0.01f) &&
              closeTo(scene.dpi, window->getDpiScale(), 0.01f) && closeTo(scene.width, window->getWidth()) &&
              // A synthesized WM_DPICHANGED keeps the 96-DPI frame borders, so the client area grows by
              // the border difference (a few DIPs); a real monitor change does not. The DIP size stays
              // within 2 %, and layout follows the window in DIPs.
              std::fabs(window->getWidth() - width) < width * 0.02f &&
              closeTo(ui.bounds(stack).width, stackWidth + (window->getWidth() - width)),
          "DPI change updates composition and keeps the layout in DIPs");

    ShowWindow(hwnd, SW_MINIMIZE);
    pump();
    check(xaml.isSuspended(), "minimized host is suspended");
    ShowWindow(hwnd, SW_RESTORE);
    pump();
    check(!xaml.isSuspended(), "restored host resumes");

    window->close();
    pump();
    check(!xaml.isAttached(), "closing the window detaches the host");
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // TSF text input needs an STA
    {
        Application app;
        windows(app);
        host(app);
    }
    lifecycle();
    CoUninitialize();
    std::printf("N01 exit=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
