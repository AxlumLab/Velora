// M04 Win32 window test: real OS resize, DPI messages, input, text input, file drop,
// idle wait/wake, clipboard and multi-window lifetime. The live IME check drives the
// installed Microsoft Pinyin TSF service through ITfKeystrokeMgr, which feeds keys to
// this thread's TSF focus only (no global input injection).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <msctf.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <vector>
#include <velora/platform/windowing/NativeWindow.hpp>
#include <windows.h>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("M04 %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

HWND hwndOf(const NativeWindow& w)
{
    return static_cast<HWND>(w.nativeHandle());
}

SIZE clientSize(HWND hwnd)
{
    RECT r{};
    GetClientRect(hwnd, &r);
    return {r.right - r.left, r.bottom - r.top};
}

template <class Pred> const WindowEvent* find(const NativeWindow& w, Pred pred)
{
    for (const auto& e : w.events())
        if (pred(e))
            return &e;
    return nullptr;
}

const WindowEvent* lastOf(const NativeWindow& w, WindowEventKind kind)
{
    const WindowEvent* found = nullptr;
    for (const auto& e : w.events())
        if (e.kind == kind)
            found = &e;
    return found;
}

HDROP makeDrop(const std::vector<std::wstring>& paths, POINT at)
{
    std::size_t chars = 1;
    for (const auto& p : paths)
        chars += p.size() + 1;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + chars * sizeof(wchar_t));
    auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
    drop->pFiles = sizeof(DROPFILES);
    drop->pt = at;
    drop->fWide = TRUE;
    auto* cursor = reinterpret_cast<wchar_t*>(drop + 1);
    for (const auto& p : paths)
    {
        std::copy(p.begin(), p.end(), cursor);
        cursor += p.size() + 1;
    }
    GlobalUnlock(memory);
    return static_cast<HDROP>(memory);
}

void basic(NativeWindow& window)
{
    const auto hwnd = hwndOf(window);
    const float scale = window.dpiScale();
    check(hwnd && scale > 0, "native handle and dpi");
    check(window.width() == MulDiv(320, GetDpiForWindow(hwnd), 96) &&
              window.height() == MulDiv(200, GetDpiForWindow(hwnd), 96),
          "logical size scaled to physical client size");

    window.setTitle("Velora 中文 😀");
    wchar_t title[64]{};
    GetWindowTextW(hwnd, title, 64);
    check(std::wstring(title) == L"Velora \u4E2D\u6587 \U0001F600", "UTF-8 title");

    window.clearEvents();
    window.resize(640, 480);
    window.pump();
    const SIZE real = clientSize(hwnd);
    const auto* resized = lastOf(window, WindowEventKind::kResized);
    check(real.cx == 640 && real.cy == 480 && window.width() == 640 && resized && resized->width == 640 &&
              resized->height == 480,
          "resize changes the real client area");

    // A size change made by the OS side is reported too.
    RECT frame{};
    GetWindowRect(hwnd, &frame);
    SetWindowPos(hwnd, nullptr, 0, 0, frame.right - frame.left + 100, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    check(window.width() == 740 && clientSize(hwnd).cx == 740, "external resize reported");
    window.resize(640, 480);
}

void dpi(NativeWindow& window)
{
    const auto hwnd = hwndOf(window);
    // WM_GETDPISCALEDSIZE keeps the logical client size: the current client area scaled from the
    // window's DPI (whatever the test machine's display uses) to 192.
    SIZE proposed{};
    SendMessageW(hwnd, WM_GETDPISCALEDSIZE, 192, reinterpret_cast<LPARAM>(&proposed));
    const UINT current = GetDpiForWindow(hwnd);
    const SIZE before = clientSize(hwnd);
    RECT expected{0, 0, MulDiv(before.cx, 192, static_cast<int>(current)),
                  MulDiv(before.cy, 192, static_cast<int>(current))};
    AdjustWindowRectExForDpi(&expected, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, 192);
    check(proposed.cx == expected.right - expected.left && proposed.cy == expected.bottom - expected.top,
          "dpi scaled size for 200%");

    // Simulated monitor change to 150%: the window adopts the suggested rect.
    window.clearEvents();
    RECT suggested{100, 100, 0, 0};
    RECT client{0, 0, 960, 720};
    AdjustWindowRectExForDpi(&client, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, 144);
    suggested.right = suggested.left + (client.right - client.left);
    suggested.bottom = suggested.top + (client.bottom - client.top);
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
    RECT actual{};
    GetWindowRect(hwnd, &actual);
    const auto* changed = lastOf(window, WindowEventKind::kDpiChanged);
    check(window.dpiScale() == 1.5f && changed && changed->dpiScale == 1.5f, "dpi change event");
    check(actual.right - actual.left == suggested.right - suggested.left &&
              lastOf(window, WindowEventKind::kResized),
          "dpi change applies suggested rect");
    SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(96, 96), reinterpret_cast<LPARAM>(&suggested));
    window.resize(640, 480);
}

void input(NativeWindow& window)
{
    const auto hwnd = hwndOf(window);
    window.clearEvents();
    PostMessageW(hwnd, WM_KEYDOWN, 'A', 0);
    PostMessageW(hwnd, WM_KEYUP, 'A', 0xC0000001);
    PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 20));
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(11, 21));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(12, 22));
    POINT screen{30, 40};
    ClientToScreen(hwnd, &screen);
    PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -2 * WHEEL_DELTA), MAKELPARAM(screen.x, screen.y));
    window.pump();
    const auto* down = find(window, [](auto& e) { return e.kind == WindowEventKind::kMouseButtonDown; });
    const auto* up = find(window, [](auto& e) { return e.kind == WindowEventKind::kMouseButtonUp; });
    const auto* wheel = find(window, [](auto& e) { return e.kind == WindowEventKind::kMouseWheel; });
    check(find(window, [](auto& e) { return e.kind == WindowEventKind::kKeyDown && e.code == 'A'; }) &&
              find(window, [](auto& e) { return e.kind == WindowEventKind::kKeyUp && e.code == 'A'; }),
          "key events");
    check(down && down->code == 1 && down->x == 11 && up && up->code == 1 && up->y == 22, "mouse buttons");
    check(wheel && wheel->wheel == -2.0f && wheel->x == 30 && wheel->y == 40,
          "mouse wheel in client coordinates");

    // WM_CHAR: surrogate pairs combine, control characters are not text, input must be started.
    window.clearEvents();
    PostMessageW(hwnd, WM_CHAR, 'x', 0);
    window.pump();
    check(window.textInputEvents().empty(), "text ignored while text input is off");
    window.startTextInput();
    for (WPARAM unit : {WPARAM('a'), WPARAM(0xD83D), WPARAM(0xDE00), WPARAM('\r'), WPARAM(0x4E2D)})
        PostMessageW(hwnd, WM_CHAR, unit, 0);
    window.pump();
    const auto& text = window.textInputEvents();
    check(text.size() == 3 && text[0].text == "a" && text[1].text == "\xF0\x9F\x98\x80" &&
              text[2].text == "\xE4\xB8\xAD",
          "WM_CHAR commits with surrogate pairs");
    window.setTextInputRect(40, 50, 2, 18);
    window.stopTextInput();
    check(window.textInputBackend() == TextInputBackend::kTsf, "TSF text input backend");
}

void fileDrop(NativeWindow& window)
{
    window.clearEvents();
    PostMessageW(
        hwndOf(window), WM_DROPFILES,
        reinterpret_cast<WPARAM>(makeDrop({L"C:\\tmp\\a.txt", L"C:\\tmp\\\u4E2D\u6587.png"}, {5, 6})), 0);
    window.pump();
    const auto* drop = find(window, [](auto& e) { return e.kind == WindowEventKind::kFileDrop; });
    check(drop && drop->paths.size() == 2 && drop->paths[0] == "C:\\tmp\\a.txt" &&
              drop->paths[1] == "C:\\tmp\\\xE4\xB8\xAD\xE6\x96\x87.png" && drop->x == 5 && drop->y == 6,
          "file drop paths");
}

void idleWait(NativeWindow& window)
{
    using clock = std::chrono::steady_clock;
    window.pump();
    bool idle = false;
    auto elapsed = clock::duration{};
    // Window creation, DPI and IME changes can post more than five batches of messages.
    // Allow the queue to settle, but still require a real 80 ms timeout below.
    const auto deadline = clock::now() + std::chrono::seconds(2);
    while (!idle && clock::now() < deadline)
    {
        const auto begin = clock::now();
        idle = !window.waitEvents(80);
        elapsed = clock::now() - begin;
    }
    check(idle && elapsed >= std::chrono::milliseconds(70), "idle wait blocks until timeout");

    while (window.waitEvents(0))
    {
    }
    std::thread waker(
        [&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            window.wake();
        });
    // Unrelated posted messages may return early; keep waiting until the wake time.
    const auto begin = clock::now();
    bool woke = true;
    while (woke && clock::now() - begin < std::chrono::milliseconds(45))
        woke = window.waitEvents(5000);
    const auto waited = clock::now() - begin;
    waker.join();
    check(woke && waited < std::chrono::milliseconds(1000), "wake from another thread");
    std::printf("M04 idle wait: timeout wait %.1f ms, wake after %.1f ms\n",
                std::chrono::duration<double, std::milli>(elapsed).count(),
                std::chrono::duration<double, std::milli>(waited).count());
}

// Live composition through the installed Microsoft Pinyin text service. Text services
// ignore keys handed to ITfKeystrokeMgr directly, so this sends real keyboard input; it
// only runs with --live-ime and stops if this window is not the foreground window.
void liveIme(NativeWindow& window, const char* backend)
{
    const CLSID pinyin{0x81D4E9C9, 0x1D3B, 0x41BC, {0x9E, 0x6C, 0x4B, 0x40, 0xBF, 0x79, 0xE3, 0x5E}};
    const GUID profile{0xFA550B04, 0x5AD7, 0x411F, {0xA5, 0xAC, 0xCA, 0x03, 0x8E, 0xC5, 0x15, 0xD7}};
    ITfInputProcessorProfileMgr* profiles = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfileMgr, reinterpret_cast<void**>(&profiles))) ||
        FAILED(profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, pinyin, profile, nullptr,
                                         TF_IPPMF_FORPROCESS | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE)))
    {
        if (profiles)
            profiles->Release();
        std::printf("M04 live IME: NOT_RUN (Microsoft Pinyin unavailable)\n");
        return;
    }
    profiles->Release();

    const HWND hwnd = hwndOf(window);
    window.show();
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    window.startTextInput();
    for (int i = 0; i < 10; ++i)
        window.waitEvents(10);
    // Open the IME in native (Chinese) mode; the service resets the mode on focus changes,
    // so this must follow focus and startTextInput.
    ITfThreadMgr* thread = nullptr;
    ITfCompartmentMgr* compartments = nullptr;
    TfClientId client = 0;
    CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr,
                     reinterpret_cast<void**>(&thread));
    thread->Activate(&client);
    if (SUCCEEDED(thread->QueryInterface(IID_ITfCompartmentMgr, reinterpret_cast<void**>(&compartments))))
    {
        for (const auto& [id, value] :
             {std::pair{GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, 1L},
              std::pair{GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, long(TF_CONVERSIONMODE_NATIVE)}})
        {
            ITfCompartment* compartment = nullptr;
            if (SUCCEEDED(compartments->GetCompartment(id, &compartment)))
            {
                VARIANT v{};
                v.vt = VT_I4;
                v.lVal = value;
                compartment->SetValue(client, &v);
                compartment->Release();
            }
        }
        compartments->Release();
    }

    for (int i = 0; i < 20; ++i)
        window.waitEvents(10);
    window.clearEvents();
    const auto press = [&](WORD vk)
    {
        if (GetForegroundWindow() != hwnd)
            return false;
        INPUT keys[2]{};
        keys[0].type = keys[1].type = INPUT_KEYBOARD;
        keys[0].ki.wVk = keys[1].ki.wVk = vk;
        keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, keys, sizeof(INPUT));
        for (int i = 0; i < 5; ++i)
            window.waitEvents(10);
        return true;
    };
    bool foreground = true;
    for (char c : std::string("ZHONG"))
        foreground = foreground && press(static_cast<WORD>(c));
    // The last composition update can trail the key; wait for it rather than a fixed time.
    for (int i = 0; i < 100 && foreground; ++i)
    {
        const auto& events = window.textInputEvents();
        if (!events.empty() && events.back().text == "zhong")
            break;
        window.waitEvents(10);
    }
    std::vector<TextInputEvent> composing = window.textInputEvents();
    window.clearEvents();
    foreground = foreground && press(VK_SPACE);
    for (int i = 0; i < 20; ++i)
        window.waitEvents(10);
    const auto finished = window.textInputEvents();
    window.stopTextInput();
    thread->Deactivate();
    thread->Release();
    if (!foreground)
    {
        std::printf("M04 live IME: NOT_RUN (window lost foreground; input stopped)\n");
        return;
    }

    for (const auto& e : composing)
        std::printf("M04 live IME %s \"%s\" cursor=%d\n",
                    e.kind == TextInputKind::kPreedit ? "preedit" : "early commit", e.text.c_str(), e.cursor);
    std::vector<std::string> commits;
    for (const auto& e : finished)
    {
        if (e.kind != TextInputKind::kCommit)
            continue;
        commits.push_back(e.text);
        std::printf("M04 live IME commit bytes:");
        for (unsigned char c : e.text)
            std::printf(" %02X", c);
        std::printf("\n");
    }
    const bool spelled = composing.size() >= 5 && composing.back().kind == TextInputKind::kPreedit &&
                         composing.back().text == "zhong" && composing.back().cursor == 5;
    std::printf("M04 live IME backend: %s\n", backend);
    check(spelled, "live preedit with caret");
    check(commits.size() == 1 && commits[0] == "\xE4\xB8\xAD", "live commit exactly once (U+4E2D)");
    check(!finished.empty() && finished.back().kind == TextInputKind::kPreedit &&
              finished.back().text.empty(),
          "live preedit cleared");
}

} // namespace

int main(int argc, char** argv)
{
    const bool live = argc > 1 && std::string(argv[1]) == "--live-ime";
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // the live check creates TSF objects itself
    WindowDesc desc;
    desc.title = "Velora platform test";
    desc.width = 320;
    desc.height = 200;
    desc.visible = false;
    {
        NativeWindow window;
        check(window.create(desc), "create");
        basic(window);
        dpi(window);
        input(window);
        fileDrop(window);
        idleWait(window);
        check(window.setClipboardText("velora \xE5\x89\xAA\xE8\xB4\xB4\xE6\x9D\xBF") &&
                  window.clipboardText() == "velora \xE5\x89\xAA\xE8\xB4\xB4\xE6\x9D\xBF",
              "UTF-8 clipboard");
        if (live)
            liveIme(window, "TSF");
        else
            std::printf("M04 live IME: NOT_RUN (pass --live-ime; sends real keyboard input)\n");

        window.clearEvents();
        PostMessageW(hwndOf(window), WM_CLOSE, 0, 0);
        window.pump();
        check(window.closed() && lastOf(window, WindowEventKind::kClosed) && !window.nativeHandle(),
              "WM_CLOSE destroys");
    }

    if (live)
    {
        WindowDesc imm = desc;
        imm.useTsf = false;
        NativeWindow window;
        check(window.create(imm) && window.textInputBackend() == TextInputBackend::kImm,
              "IMM fallback backend");
        liveIme(window, "IMM");
    }

    NativeWindow first, second;
    check(first.create(desc) && second.create(desc) && first.nativeHandle() != second.nativeHandle(),
          "two windows");
    first.close();
    second.pump();
    check(first.closed() && !second.closed() && IsWindow(hwndOf(second)),
          "closing one window keeps the other");
    second.close();
    second.pump();
    check(second.closed(), "second window closed");

    std::printf("M04 platform exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
