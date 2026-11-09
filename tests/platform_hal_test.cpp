// Platform HAL (R01.2): the Dispatcher contract.
//
// Dispatcher is the UI thread's message queue (the EventLoop alias was removed in R12.1). The object runtime relies on
// isOwnerThread() for thread affinity (R00-Contracts C-1.8), and the application relies on
// wake() reaching a blocked wait from another thread, so both are checked on a real queue.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <velora/platform/dispatcher/Dispatcher.hpp>
#include <velora/platform/windowing/NativeWindow.hpp>

using namespace velora;

namespace
{
int g_failures = 0;

void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<Dispatcher>);

    Dispatcher dispatcher;
    check(dispatcher.isOwnerThread(), "the creating thread owns the dispatcher");

    std::atomic<bool> otherOwns{true};
    std::thread([&] { otherOwns = dispatcher.isOwnerThread(); }).join();
    check(!otherOwns, "another thread does not own the dispatcher");

    // An empty queue times out instead of spinning or blocking forever.
    check(!dispatcher.waitEvents(0), "an empty queue reports no message");

    // wake() from another thread ends a wait that would otherwise last the full timeout.
    const auto started = std::chrono::steady_clock::now();
    std::thread waker(
        [&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            dispatcher.wake();
        });
    const bool woke = dispatcher.waitEvents(5000);
    waker.join();
    const auto waited = std::chrono::steady_clock::now() - started;
    check(woke, "a cross-thread wake ends the wait");
    check(waited < std::chrono::seconds(4), "the wait ended on the wake, not on the timeout");

    // wakeCallback() is usable from another thread too.
    auto callback = dispatcher.wakeCallback();
    std::thread([callback] { callback(); }).join();
    check(dispatcher.waitEvents(1000), "the wake callback reaches the queue");

    dispatcher.pump(); // nothing pending: must return at once

    // A window reports its platform surface: the borrowed handle and the size in device pixels.
    NativeWindow window;
    check(!window.surface().handle, "no surface before the window exists");
    WindowDesc desc;
    desc.visible = false;
    desc.width = 320;
    desc.height = 200;
    if (window.create(desc))
    {
        const SurfaceDescriptor surface = window.surface();
        check(surface.handle == window.nativeHandle(), "the surface borrows the window handle");
        check(surface.width == static_cast<u32>(window.width()) &&
                  surface.height == static_cast<u32>(window.height()),
              "the surface size is the client size in device pixels");
        check(surface.dpiScale == window.dpiScale() && surface.dpiScale > 0,
              "the surface carries the DPI scale");
        window.close();
        check(!window.surface().handle, "no surface after the window closed");
    }
    else
    {
        check(false, "a hidden window can be created");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
