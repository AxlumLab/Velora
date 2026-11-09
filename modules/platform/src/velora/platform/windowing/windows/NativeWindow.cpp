// NativeWindow API (Win32). Dispatching messages belongs to the thread queue, not the window,
// and lives in dispatcher/windows/Dispatcher.cpp.
#include "velora/platform/windowing/windows/NativeWindowInternal.hpp"

#include <algorithm>
#include <imm.h>
#include <mutex>
#include <velora/platform/dispatcher/ThreadQueue.hpp>

namespace velora
{

namespace
{
void registerClass(WNDPROC proc)
{
    static std::once_flag once;
    std::call_once(once,
                   [proc]
                   {
                       // Must precede window creation; fails harmlessly if the process already chose a mode.
                       SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
                       WNDCLASSW wc{};
                       wc.lpfnWndProc = proc;
                       wc.hInstance = GetModuleHandleW(nullptr);
                       wc.lpszClassName = kWindowClassName;
                       wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(IDC_ARROW));
                       RegisterClassW(&wc);
                   });
}

DWORD windowStyle(bool resizable)
{
    return resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
}
} // namespace

SIZE frameSize(int clientWidth, int clientHeight, DWORD style, UINT dpi)
{
    RECT r{0, 0, clientWidth, clientHeight};
    AdjustWindowRectExForDpi(&r, style, FALSE, WS_EX_ACCEPTFILES, dpi);
    return {r.right - r.left, r.bottom - r.top};
}

std::string lastErrorMessage(const char* what)
{
    const DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string message = std::string(what) + " failed (error " + std::to_string(code) + ")";
    if (text)
    {
        auto detail = toUtf8(text);
        while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r' || detail.back() == ' '))
            detail.pop_back();
        message += ": " + detail;
        LocalFree(text);
    }
    return message;
}

// ---- NativeWindow ---------------------------------------------------------------------

NativeWindow::NativeWindow() : m_impl(std::make_unique<Impl>()) {}

NativeWindow::~NativeWindow()
{
    close();
}

bool NativeWindow::create(const WindowDesc& desc)
{
    auto& impl = *m_impl;
    if (impl.hwnd)
    {
        impl.error = "the window already exists";
        return false;
    }
    registerClass(&Impl::proc);
    impl.style = windowStyle(desc.resizable);
    const UINT systemDpi = GetDpiForSystem();
    const SIZE frame =
        frameSize(MulDiv(desc.width, systemDpi, USER_DEFAULT_SCREEN_DPI),
                  MulDiv(desc.height, systemDpi, USER_DEFAULT_SCREEN_DPI), impl.style, systemDpi);
    const auto title = toUtf16(desc.title);
    if (!CreateWindowExW(WS_EX_ACCEPTFILES, kWindowClassName, title.c_str(), impl.style, CW_USEDEFAULT,
                         CW_USEDEFAULT, frame.cx, frame.cy, nullptr, nullptr, GetModuleHandleW(nullptr),
                         &impl))
    {
        impl.error = lastErrorMessage("CreateWindowExW");
        return false;
    }
    impl.error.clear();
    impl.closed = false;
    impl.dpi = GetDpiForWindow(impl.hwnd);
    if (impl.dpi != systemDpi)
    {
        // Created on a monitor whose DPI differs from the system DPI.
        const SIZE fixed =
            frameSize(MulDiv(desc.width, impl.dpi, USER_DEFAULT_SCREEN_DPI),
                      MulDiv(desc.height, impl.dpi, USER_DEFAULT_SCREEN_DPI), impl.style, impl.dpi);
        SetWindowPos(impl.hwnd, nullptr, 0, 0, fixed.cx, fixed.cy,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    impl.updateSize();
    // An elevated process otherwise rejects drops from a non-elevated Explorer.
    ChangeWindowMessageFilterEx(impl.hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(impl.hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(impl.hwnd, 0x0049 /* WM_COPYGLOBALDATA */, MSGFLT_ALLOW, nullptr);
    if (desc.useTsf)
        impl.tsf = TsfTextInput::create(
            impl.hwnd, {[this](const std::wstring& text, int caret, int selection)
                        { m_impl->preedit(text, caret, selection); },
                        [this](const std::wstring& text) { m_impl->commit(text); },
                        [this]
                        {
                            RECT r = m_impl->textRect;
                            MapWindowPoints(m_impl->hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
                            return r;
                        }});
    ImmAssociateContextEx(impl.hwnd, nullptr, 0); // IME stays off until startTextInput
    if (desc.visible)
        show();
    return true;
}

const std::string& NativeWindow::lastError() const noexcept
{
    return m_impl->error;
}

void NativeWindow::setEventSink(const WindowEventSink& sink)
{
    m_impl->sink = sink;
}

void NativeWindow::show()
{
    if (m_impl->hwnd)
        ShowWindow(m_impl->hwnd, SW_SHOW);
}

void NativeWindow::hide()
{
    if (m_impl->hwnd)
        ShowWindow(m_impl->hwnd, SW_HIDE);
}

void NativeWindow::setTitle(std::string_view title)
{
    if (m_impl->hwnd)
        SetWindowTextW(m_impl->hwnd, toUtf16(title).c_str());
}

void NativeWindow::resize(int width, int height)
{
    if (!m_impl->hwnd || width <= 0 || height <= 0)
        return;
    const SIZE frame = frameSize(width, height, m_impl->style, m_impl->dpi);
    SetWindowPos(m_impl->hwnd, nullptr, 0, 0, frame.cx, frame.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void NativeWindow::requestClose()
{
    if (m_impl->hwnd)
        SendMessageW(m_impl->hwnd, WM_CLOSE, 0, 0);
}

void NativeWindow::close()
{
    if (m_impl->hwnd)
        DestroyWindow(m_impl->hwnd);
    m_impl->closed = true;
}

void NativeWindow::pump()
{
    dispatchThreadMessages(0);
}

bool NativeWindow::waitEvents(int timeoutMs)
{
    return dispatchThreadMessages(timeoutMs);
}

void NativeWindow::wake() const
{
    if (m_impl->hwnd)
        PostMessageW(m_impl->hwnd, kWakeMessage, 0, 0);
}

void NativeWindow::startTextInput()
{
    if (!m_impl->hwnd)
        return;
    m_impl->textInput = true;
    if (m_impl->tsf)
        m_impl->tsf->setEnabled(true);
    ImmAssociateContextEx(m_impl->hwnd, nullptr, IACE_DEFAULT);
    m_impl->positionIme();
}

void NativeWindow::stopTextInput()
{
    if (!m_impl->hwnd)
        return;
    m_impl->textInput = false;
    if (m_impl->tsf)
        m_impl->tsf->setEnabled(false);
    ImmAssociateContextEx(m_impl->hwnd, nullptr, 0);
}

bool NativeWindow::textInputActive() const noexcept
{
    return m_impl->textInput;
}

TextInputBackend NativeWindow::textInputBackend() const noexcept
{
    return !m_impl->hwnd ? TextInputBackend::kNone
           : m_impl->tsf ? TextInputBackend::kTsf
                         : TextInputBackend::kImm;
}

void NativeWindow::setTextInputRect(int x, int y, int width, int height)
{
    m_impl->textRect = {x, y, x + std::max(width, 1), y + std::max(height, 1)};
    if (m_impl->hwnd)
        m_impl->positionIme();
}

void NativeWindow::setCursor(CursorKind cursor)
{
    const wchar_t* id = IDC_ARROW;
    switch (cursor)
    {
    case CursorKind::kArrow:
        id = IDC_ARROW;
        break;
    case CursorKind::kIBeam:
        id = IDC_IBEAM;
        break;
    case CursorKind::kHand:
        id = IDC_HAND;
        break;
    case CursorKind::kWait:
        id = IDC_WAIT;
        break;
    case CursorKind::kSizeAll:
        id = IDC_SIZEALL;
        break;
    }
    m_impl->cursor = LoadCursorW(nullptr, id);
    // Apply now when the pointer is over the client area; WM_SETCURSOR keeps it there.
    POINT p{};
    RECT client{};
    if (m_impl->hwnd && GetCursorPos(&p) && ScreenToClient(m_impl->hwnd, &p) &&
        GetClientRect(m_impl->hwnd, &client) && PtInRect(&client, p))
        SetCursor(m_impl->cursor);
}

void* NativeWindow::nativeHandle() const noexcept
{
    return m_impl->hwnd;
}

int NativeWindow::width() const noexcept
{
    return m_impl->width;
}

int NativeWindow::height() const noexcept
{
    return m_impl->height;
}

float NativeWindow::dpiScale() const noexcept
{
    return static_cast<float>(m_impl->dpi) / USER_DEFAULT_SCREEN_DPI;
}

bool NativeWindow::closed() const noexcept
{
    return m_impl->closed;
}

bool NativeWindow::visible() const noexcept
{
    return m_impl->visible;
}

bool NativeWindow::minimized() const noexcept
{
    return m_impl->minimized;
}

const std::vector<WindowEvent>& NativeWindow::events() const noexcept
{
    return m_impl->events;
}

const std::vector<TextInputEvent>& NativeWindow::textInputEvents() const noexcept
{
    return m_impl->textEvents;
}

void NativeWindow::clearEvents() noexcept
{
    m_impl->events.clear();
    m_impl->textEvents.clear();
}

SurfaceDescriptor NativeWindow::surface() const noexcept
{
    return {m_impl->hwnd, static_cast<u32>(std::max(m_impl->width, 0)),
            static_cast<u32>(std::max(m_impl->height, 0)), dpiScale(), false};
}

bool NativeWindow::setClipboardText(std::string_view text) const
{
    if (!OpenClipboard(m_impl->hwnd))
        return false;
    EmptyClipboard();
    const auto wide = toUtf16(text);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (!memory)
    {
        CloseClipboard();
        return false;
    }
    auto* target = static_cast<wchar_t*>(GlobalLock(memory));
    std::copy(wide.begin(), wide.end(), target);
    target[wide.size()] = L'\0';
    GlobalUnlock(memory);
    if (!SetClipboardData(CF_UNICODETEXT, memory))
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

std::string NativeWindow::clipboardText() const
{
    std::string result;
    if (!OpenClipboard(m_impl->hwnd))
        return result;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT))
    {
        if (const auto* source = static_cast<const wchar_t*>(GlobalLock(handle)))
        {
            result = toUtf8(source);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

} // namespace velora
