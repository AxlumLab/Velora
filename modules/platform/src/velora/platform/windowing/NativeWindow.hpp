#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <velora/globals.hpp>
#include <velora/platform/ime/ImeEvents.hpp>
#include <velora/platform/input/InputEvents.hpp>
#include <velora/platform/surface/SurfaceDescriptor.hpp>

// Platform HAL — native window (Win32 on Windows).
//
// Creation sizes are DIPs; event positions and sizes are physical client pixels (divide by
// dpiScale() for DIPs). The window reports events and nothing more: it does not hit-test, does
// not route to controls and does not know about XAML.

namespace velora
{

struct WindowDesc
{
    std::string title = "Velora";
    int width = 800;
    int height = 600;
    bool visible = true;
    bool resizable = true;
    bool useTsf = true;
};

enum class CursorKind
{
    kArrow,
    kIBeam,
    kHand,
    kWait,
    kSizeAll
};

// Receives events synchronously from the window procedure, so they also arrive inside the
// modal move/size loop. Plain function pointers keep this boundary free of vtables and
// std::function. Without a sink, events are queued (events(), textInputEvents()).
struct WindowEventSink
{
    void* context = nullptr;
    void (*onWindowEvent)(void* context, WindowEvent& event) = nullptr;
    void (*onTextInput)(void* context, const TextInputEvent& event) = nullptr;
};

class NativeWindow final
{
public:
    NativeWindow();
    ~NativeWindow();

    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    bool create(const WindowDesc& desc);
    // The message describing why the last create() failed.
    [[nodiscard]] const std::string& lastError() const noexcept;
    void setEventSink(const WindowEventSink& sink);

    void show();
    void hide();
    void setTitle(std::string_view title);
    // Requests physical-pixel dimensions; emits Resized event.
    void resize(int width, int height);
    // Asks to close: emits kCloseRequested, then closes unless a sink cancels.
    void requestClose();
    // Destroys the window immediately.
    void close();
    // Dispatches the calling thread's pending messages; see dispatcher/Dispatcher.hpp.
    void pump();
    // Negative timeout waits forever; returns false on timeout.
    bool waitEvents(int timeoutMs);
    void wake() const;

    void startTextInput();
    void stopTextInput();
    bool textInputActive() const noexcept;
    TextInputBackend textInputBackend() const noexcept;
    // Caret rectangle in client pixels; positions the IME composition and candidate windows.
    void setTextInputRect(int x, int y, int width, int height);
    void setCursor(CursorKind cursor);

    [[nodiscard]] void* nativeHandle() const noexcept;
    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] float dpiScale() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] bool minimized() const noexcept;
    [[nodiscard]] const std::vector<WindowEvent>& events() const noexcept;
    [[nodiscard]] const std::vector<TextInputEvent>& textInputEvents() const noexcept;
    void clearEvents() noexcept;

    // The platform surface a renderer may attach to: the borrowed window handle plus the
    // current size and DPI. Valid until the window closes.
    [[nodiscard]] SurfaceDescriptor surface() const noexcept;

    bool setClipboardText(std::string_view text) const;
    std::string clipboardText() const;

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace velora
