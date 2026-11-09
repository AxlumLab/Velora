#pragma once

#include <velora/globals.hpp>

// Internal to the platform module: NativeWindow's Win32 state, shared by
// windowning/windows/NativeWindow.cpp (window API) and windowning/windows/WindowProc.cpp
// (message handling).
#include "velora/platform/ime/windows/TsfTextInput.hpp"
#include "velora/platform/windowing/NativeWindow.hpp"

#include <windows.h>

namespace velora
{

inline constexpr wchar_t kWindowClassName[] = L"VeloraNativeWindow";
inline constexpr UINT kWakeMessage = WM_APP + 1;

u32 currentModifiers();
SIZE frameSize(int clientWidth, int clientHeight, DWORD style, UINT dpi);
std::string lastErrorMessage(const char* what);

struct NativeWindow::Impl
{
    HWND hwnd = nullptr;
    DWORD style = 0;
    int width = 0;
    int height = 0;
    UINT dpi = USER_DEFAULT_SCREEN_DPI;
    bool closed = true;
    bool visible = false;
    bool minimized = false;
    bool textInput = false;
    wchar_t highSurrogate = 0;
    RECT textRect{0, 0, 1, 1};
    HCURSOR cursor = nullptr;
    std::string error;
    WindowEventSink sink;
    std::unique_ptr<TsfTextInput> tsf;
    std::vector<WindowEvent> events;
    std::vector<TextInputEvent> textEvents;

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle(UINT message, WPARAM wparam, LPARAM lparam);

    // Delivers to the sink, or queues without one. Returns the (possibly sink-modified) event.
    WindowEvent push(WindowEvent event);
    void pushText(TextInputEvent event);
    void commit(const std::wstring& text);
    void preedit(const std::wstring& text, int caret, int selection);
    void updateSize();
    void positionIme();
    std::wstring immString(HIMC context, DWORD kind);
};

} // namespace velora
