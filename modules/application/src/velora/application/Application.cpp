#include "velora/application/Application.hpp"

#include <algorithm>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <velora/platform/dispatcher/Dispatcher.hpp>
#else
namespace velora
{
// No native backend: a dispatcher that never receives anything. `velora::platform` is
// Windows-only for now, so this keeps the application buildable elsewhere.
class Dispatcher final
{
public:
    bool waitEvents(int)
    {
        return false;
    }
    void pump() {}
    void wake() noexcept {}
    std::function<void()> wakeCallback() const
    {
        return [] {};
    }
    bool isOwnerThread() const noexcept
    {
        return m_thread == std::this_thread::get_id();
    }

private:
    std::thread::id m_thread = std::this_thread::get_id();
};
} // namespace velora
#endif

namespace velora
{

Application::Application() : m_loop(std::make_unique<Dispatcher>()) {}

Application::~Application()
{
    // Windows close (and report kClosed) while the application still exists.
    for (auto& window : m_windows)
        window->close();
    m_windows.clear();
}

AppWindow* Application::createWindow(const WindowOptions& options)
{
    if (!isUiThread())
    {
        m_error = "createWindow must be called on the UI thread";
        return nullptr;
    }
    auto window = std::make_unique<AppWindow>();
    if (!window->create(options))
    {
        m_error = window->lastError();
        return nullptr;
    }
    m_error.clear();
    return m_windows.emplace_back(std::move(window)).get();
}

std::vector<AppWindow*> Application::getWindows() const
{
    std::vector<AppWindow*> out;
    for (const auto& window : m_windows)
        out.push_back(window.get());
    return out;
}

std::size_t Application::getOpenWindowCount() const noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(m_windows, [](const auto& window) { return !window->isClosed(); }));
}

void Application::setLaunchHandler(LaunchHandler handler)
{
    m_onLaunch = std::move(handler);
}

void Application::setShutdownMode(ShutdownMode mode) noexcept
{
    m_shutdownMode = mode;
}

void Application::setFrameCallback(FrameCallback callback)
{
    m_onFrame = std::move(callback);
}

i32 Application::run()
{
    if (!isUiThread() || m_running)
        return -1;
    m_running = true;
    m_exitRequested.store(false, std::memory_order_release);
    m_exitCode.store(0, std::memory_order_release);
    if (m_onLaunch)
        m_onLaunch(*this);
    while (!m_exitRequested.load(std::memory_order_acquire))
    {
        if (m_shutdownMode == ShutdownMode::kOnLastWindowClose && getOpenWindowCount() == 0)
            break;
        // Tick frame schedulers before waiting; block only when no work remains.
        const bool hasPendingWork = m_onFrame ? m_onFrame() : false;
        const int timeout = hasPendingWork ? 0 : -1;
        (void)m_loop->waitEvents(timeout);
    }
    m_running = false;
    return m_exitCode.load(std::memory_order_acquire);
}

void Application::exit(i32 exitCode) noexcept
{
    m_exitCode.store(exitCode, std::memory_order_release);
    m_exitRequested.store(true, std::memory_order_release);
    m_loop->wake();
}

std::function<void()> Application::wakeCallback() const
{
    return m_loop->wakeCallback();
}

bool Application::isUiThread() const noexcept
{
    return m_loop->isOwnerThread();
}

bool Application::isRunning() const noexcept
{
    return m_running;
}

const std::string& Application::lastError() const noexcept
{
    return m_error;
}

} // namespace velora
