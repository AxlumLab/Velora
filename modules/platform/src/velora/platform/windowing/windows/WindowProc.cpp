#include <velora/globals.hpp>
// NativeWindow message handling: Win32 messages become WindowEvent/TextInputEvent.
#include "velora/platform/windowing/windows/NativeWindowInternal.hpp"

#include <algorithm>
#include <imm.h>
#include <shellapi.h>
#include <windowsx.h>

namespace velora
{

u32 currentModifiers()
{
    u32 modifiers = kModifierNone;
    if (GetKeyState(VK_SHIFT) < 0)
        modifiers |= kModifierShift;
    if (GetKeyState(VK_CONTROL) < 0)
        modifiers |= kModifierControl;
    if (GetKeyState(VK_MENU) < 0)
        modifiers |= kModifierAlt;
    return modifiers;
}

WindowEvent NativeWindow::Impl::push(WindowEvent event)
{
    if (sink.onWindowEvent)
        sink.onWindowEvent(sink.context, event);
    else
        events.push_back(event);
    return event;
}

void NativeWindow::Impl::pushText(TextInputEvent event)
{
    if (sink.onTextInput)
        sink.onTextInput(sink.context, event);
    else
        textEvents.push_back(std::move(event));
}

void NativeWindow::Impl::commit(const std::wstring& text)
{
    if (!text.empty())
        pushText({TextInputKind::kCommit, toUtf8(text)});
}

void NativeWindow::Impl::preedit(const std::wstring& text, int caret, int selection)
{
    const auto prefix = toUtf8(std::wstring_view(text).substr(0, static_cast<std::size_t>(caret)));
    const auto selected = toUtf8(
        std::wstring_view(text).substr(static_cast<std::size_t>(caret), static_cast<std::size_t>(selection)));
    pushText({TextInputKind::kPreedit, toUtf8(text), static_cast<int>(prefix.size()),
              static_cast<int>(selected.size())});
}

void NativeWindow::Impl::updateSize()
{
    RECT r{};
    GetClientRect(hwnd, &r);
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w > 0 && h > 0 && (w != width || h != height))
    {
        width = w;
        height = h;
        push({WindowEventKind::kResized, w, h});
    }
}

void NativeWindow::Impl::positionIme()
{
    if (HIMC context = ImmGetContext(hwnd))
    {
        COMPOSITIONFORM composition{CFS_POINT, {textRect.left, textRect.top}, {}};
        ImmSetCompositionWindow(context, &composition);
        CANDIDATEFORM candidate{0, CFS_EXCLUDE, {textRect.left, textRect.bottom}, textRect};
        ImmSetCandidateWindow(context, &candidate);
        ImmReleaseContext(hwnd, context);
    }
}

std::wstring NativeWindow::Impl::immString(HIMC context, DWORD kind)
{
    const LONG bytes = ImmGetCompositionStringW(context, kind, nullptr, 0);
    if (bytes <= 0)
        return {};
    std::wstring value(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
    ImmGetCompositionStringW(context, kind, value.data(), static_cast<DWORD>(bytes));
    return value;
}

LRESULT CALLBACK NativeWindow::Impl::proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        auto* impl = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        impl->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
    }
    auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return impl ? impl->handle(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT NativeWindow::Impl::handle(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case kWakeMessage:
        return 0;
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED)
        {
            if (!minimized)
            {
                minimized = true;
                push({WindowEventKind::kMinimized});
            }
            return 0;
        }
        if (minimized)
        {
            minimized = false;
            push({WindowEventKind::kRestored});
        }
        updateSize();
        return 0;
    case WM_SHOWWINDOW:
        if ((wparam != 0) != visible)
        {
            visible = wparam != 0;
            push({visible ? WindowEventKind::kShown : WindowEventKind::kHidden});
        }
        break;
    case WM_GETDPISCALEDSIZE:
    {
        // Keep the logical client size when moving between monitors.
        const UINT next = static_cast<UINT>(wparam);
        *reinterpret_cast<SIZE*>(lparam) =
            frameSize(MulDiv(width, next, dpi), MulDiv(height, next, dpi), style, next);
        return TRUE;
    }
    case WM_DPICHANGED:
    {
        dpi = HIWORD(wparam);
        push({.kind = WindowEventKind::kDpiChanged,
              .dpiScale = static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI});
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && cursor)
        {
            SetCursor(cursor);
            return TRUE;
        }
        break;
    case WM_SETFOCUS:
        push({WindowEventKind::kFocusGained});
        return 0;
    case WM_KILLFOCUS:
        push({WindowEventKind::kFocusLost});
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        push({.kind = WindowEventKind::kKeyDown,
              .code = static_cast<u32>(wparam),
              .modifiers = currentModifiers()});
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        push({.kind = WindowEventKind::kKeyUp,
              .code = static_cast<u32>(wparam),
              .modifiers = currentModifiers()});
        break;
    case WM_MOUSEMOVE:
        push({.kind = WindowEventKind::kMouseMove,
              .x = GET_X_LPARAM(lparam),
              .y = GET_Y_LPARAM(lparam),
              .modifiers = currentModifiers()});
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    {
        const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
        const u32 button = (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)   ? 1
                           : (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) ? 2
                                                                                    : 3;
        // Keep receiving moves and the release while dragging outside the window.
        if (down)
            SetCapture(hwnd);
        else if (!(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))
            ReleaseCapture();
        push({.kind = down ? WindowEventKind::kMouseButtonDown : WindowEventKind::kMouseButtonUp,
              .code = button,
              .x = GET_X_LPARAM(lparam),
              .y = GET_Y_LPARAM(lparam),
              .modifiers = currentModifiers()});
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        POINT p{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd, &p);
        push({.kind = WindowEventKind::kMouseWheel,
              .x = p.x,
              .y = p.y,
              .wheel = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA,
              .modifiers = currentModifiers()});
        return 0;
    }
    case WM_CHAR:
    {
        // UTF-16 units; control characters belong to key events, not text.
        const auto unit = static_cast<wchar_t>(wparam);
        if (IS_HIGH_SURROGATE(unit))
        {
            highSurrogate = unit;
            return 0;
        }
        std::wstring text;
        if (IS_LOW_SURROGATE(unit))
        {
            if (highSurrogate)
                text = {highSurrogate, unit};
        }
        else if (unit >= 0x20 && unit != 0x7F)
        {
            text = {unit};
        }
        highSurrogate = 0;
        if (textInput)
            commit(text);
        return 0;
    }
    case WM_IME_STARTCOMPOSITION:
        positionIme();
        break;
    case WM_IME_COMPOSITION:
    {
        // IMM fallback (TSF clients receive compositions through the text store instead).
        HIMC context = ImmGetContext(hwnd);
        if (!context)
            break;
        if (lparam & GCS_COMPSTR)
        {
            const auto text = immString(context, GCS_COMPSTR);
            const LONG caret = (lparam & GCS_CURSORPOS)
                                   ? ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0)
                                   : static_cast<LONG>(text.size());
            preedit(text, std::clamp(static_cast<int>(caret), 0, static_cast<int>(text.size())), 0);
        }
        const bool result = (lparam & GCS_RESULTSTR) != 0;
        if (result)
            commit(immString(context, GCS_RESULTSTR));
        ImmReleaseContext(hwnd, context);
        // Handling the result here stops DefWindowProc from re-sending it as WM_IME_CHAR/WM_CHAR.
        if (result)
            return 0;
        break;
    }
    case WM_IME_ENDCOMPOSITION:
        preedit({}, 0, 0);
        break;
    case WM_DROPFILES:
    {
        auto* drop = reinterpret_cast<HDROP>(wparam);
        WindowEvent event{WindowEventKind::kFileDrop};
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i)
        {
            std::wstring path(DragQueryFileW(drop, i, nullptr, 0), L'\0');
            DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size() + 1));
            event.paths.push_back(toUtf8(path));
        }
        POINT p{};
        DragQueryPoint(drop, &p);
        event.x = p.x;
        event.y = p.y;
        DragFinish(drop);
        push(std::move(event));
        return 0;
    }
    case WM_CLOSE:
        if (!push({WindowEventKind::kCloseRequested}).cancel)
            DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (tsf)
            tsf->destroy();
        closed = true;
        visible = false;
        push({WindowEventKind::kClosed});
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        hwnd = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace velora
