#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <velora/application/AppWindow.hpp>

namespace velora
{

class Dispatcher;

enum class ShutdownMode : u8
{
    kOnLastWindowClose, // run() returns 0 once every window has closed
    kExplicit,          // run() returns only through exit()
};

// The application object. Construct it on the UI thread; windows, events and handlers live on
// that thread. run() waits for events without spinning; exit() may be called from any thread.
// The frame callback is invoked once per event loop iteration; use it to tick XamlHosts.
class Application final
{
public:
    using LaunchHandler = std::function<void(Application&)>;
    using FrameCallback = std::function<bool()>; // return true when work remains

    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Owned by the application until it is destroyed (also after the window closed).
    // Returns null on failure; lastError() tells why.
    [[nodiscard]] AppWindow* createWindow(const WindowOptions& options);
    [[nodiscard]] std::vector<AppWindow*> getWindows() const;
    [[nodiscard]] std::size_t getOpenWindowCount() const noexcept;

    void setLaunchHandler(LaunchHandler handler);
    void setShutdownMode(ShutdownMode mode) noexcept;
    // Called once per event loop iteration to tick frame schedulers. Return true when work remains.
    void setFrameCallback(FrameCallback callback);
    // Calls the launch handler, then dispatches events until exit() or, by default, until the
    // last window closed. Returns the exit code; -1 when called off the UI thread or re-entered.
    [[nodiscard]] i32 run();
    void exit(i32 exitCode = 0) noexcept;
    std::function<void()> wakeCallback() const;

    [[nodiscard]] bool isUiThread() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

private:
    std::unique_ptr<Dispatcher> m_loop;
    std::vector<std::unique_ptr<AppWindow>> m_windows; // destroyed before the loop
    LaunchHandler m_onLaunch;
    FrameCallback m_onFrame;
    ShutdownMode m_shutdownMode = ShutdownMode::kOnLastWindowClose;
    std::atomic<bool> m_exitRequested{false};
    std::atomic<i32> m_exitCode{0};
    bool m_running = false;
    std::string m_error;
};

} // namespace velora
