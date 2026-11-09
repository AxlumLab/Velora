#pragma once

#include <functional>
#include <velora/globals.hpp>

// Internal to the platform module: the calling thread's message queue.
//
// Dispatching messages is a *thread* operation, not a window operation — one queue serves every
// window the thread created. NativeWindow::pump/waitEvents and Dispatcher both need it, so it
// lives here rather than being duplicated in each translation unit, and there is exactly one
// implementation per platform backend (see dispatcher/windows/Dispatcher.cpp).

namespace velora
{

// Dispatches every pending message of the calling thread. Returns true when at least one was
// dispatched. `timeoutMs` < 0 means "wait forever"; otherwise waits up to that long for a
// message that is not already pending.
bool dispatchThreadMessages(int timeoutMs);

// Posts a wake to the calling thread's queue, so a blocked dispatchThreadMessages returns.
// Prefers a message-only window when `wakeWindow` is non-null: wakes posted there are not lost
// while a modal loop (window move/size, menus) is running.
void wakeThread(void* wakeWindow, u32 threadId) noexcept;

// Returns the platform thread identity a Dispatcher was created on. Stable for the thread's
// lifetime and never reused while the thread runs.
u32 currentThreadId() noexcept;

} // namespace velora
