#pragma once

#include <functional>
#include <memory>
#include <velora/globals.hpp>

// Platform HAL — UI thread dispatcher.
//
// Dispatcher owns the calling thread's message queue: it waits without spinning, dispatches
// every message of the thread, and can be woken from any thread. Create it on the UI thread;
// isOwnerThread() lets callers check whether work is running on that UI thread.
//
// Only one Dispatcher per UI thread is meaningful: a second instance on the same thread would
// wait on the same queue.

namespace velora
{

class Dispatcher final
{
public:
    Dispatcher();
    ~Dispatcher();
    Dispatcher(const Dispatcher&) = delete;
    Dispatcher& operator=(const Dispatcher&) = delete;

    // Dispatches pending messages, waiting up to timeoutMs (negative: forever) for the first.
    // Returns false when it timed out without any message.
    bool waitEvents(int timeoutMs);
    // Dispatches every pending message of this thread without waiting.
    void pump();
    // Thread safe. Ends the current or next wait.
    void wake() noexcept;
    // A callable that wakes the dispatcher; safe to hold beyond the dispatcher's lifetime only
    // if the caller also holds the dispatcher alive.
    std::function<void()> wakeCallback() const;
    [[nodiscard]] bool isOwnerThread() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace velora
