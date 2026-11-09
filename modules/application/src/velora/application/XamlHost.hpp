#pragma once

#include <memory>
#include <velora/application/AppWindow.hpp>

namespace velora
{

class Compositor;
class Framework;
class GraphicsDevice;
class FrameScheduler;
class NativeSink;

// Hosts a retained UI (Framework) in a window: routes the window's events, in order, to the
// framework's input (pointer, wheel, keys, IME preedit/commit), keeps the composition viewport
// and DPI in step with the window, suspends while minimized, turns IME text input on only
// while a TextBox has focus, with the candidate window at its caret, and drives the frame
// scheduler to present composition snapshots through the graphics surface.
// Attaching takes over the window's event handler; setEventHandler receives every event after
// the host handled it (e.g. to cancel kCloseRequested). The window, framework, compositor
// and graphics device must outlive the attachment; it ends with detach() or when the window closes.
class XamlHost final
{
public:
    XamlHost();
    ~XamlHost();
    XamlHost(const XamlHost&) = delete;
    XamlHost& operator=(const XamlHost&) = delete;

    [[nodiscard]] bool attach(AppWindow& window, Framework& framework, Compositor& compositor,
                              GraphicsDevice& device) noexcept;
    void detach() noexcept;
    // Adopt a root for lifetime only; removing it or closing/detaching the host closes its page.
    bool setRoot(u64 element);
    [[nodiscard]] bool isAttached() const noexcept;
    void setEventHandler(AppWindow::EventHandler handler);

    // Routes one window event; called by the window's handler while attached.
    void dispatch(AppEvent& event);
    // Lays out the UI at the window's DIP size, updates the IME state, and ticks the frame scheduler.
    void update();
    // True while the window is minimized: nothing needs to be drawn.
    [[nodiscard]] bool isSuspended() const noexcept;

    // True when the host should keep ticking instead of blocking on platform events.
    [[nodiscard]] bool hasPendingWork() const noexcept;

    const std::string& lastError() const noexcept;

private:
    void resizeViewport();
    void syncTextInput();
    void tick();

    u64 m_root = 0;
    AppWindow* m_window = nullptr;
    Framework* m_framework = nullptr;
    Compositor* m_compositor = nullptr;
    std::unique_ptr<NativeSink> m_sink;
    std::unique_ptr<FrameScheduler> m_scheduler;
    AppWindow::EventHandler m_handler;
    bool m_suspended = false;
    std::string m_error;
};

} // namespace velora
