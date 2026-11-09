// Dispatcher and the calling thread's message queue (Win32).
#include <mutex>
#include <velora/platform/dispatcher/Dispatcher.hpp>
#include <velora/platform/dispatcher/ThreadQueue.hpp>
#include <windows.h>

namespace velora
{

namespace
{
// A message-only window of our own class: wakes posted here are not lost while a modal loop
// (window move/size, menus) is running, which the thread queue alone cannot guarantee.
void registerWakeClass()
{
    static std::once_flag once;
    std::call_once(once,
                   []
                   {
                       WNDCLASSW wc{};
                       wc.lpfnWndProc = DefWindowProcW;
                       wc.hInstance = GetModuleHandleW(nullptr);
                       wc.lpszClassName = L"VeloraDispatchQueue";
                       RegisterClassW(&wc);
                   });
}

void pumpThread()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
} // namespace

bool dispatchThreadMessages(int timeoutMs)
{
    MSG message{};
    if (!PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE))
    {
        const DWORD timeout = timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs);
        if (MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE) !=
            WAIT_OBJECT_0)
            return false;
    }
    pumpThread();
    return true;
}

u32 currentThreadId() noexcept
{
    return static_cast<u32>(GetCurrentThreadId());
}

void wakeThread(void* wakeWindow, u32 threadId) noexcept
{
    if (wakeWindow)
        PostMessageW(static_cast<HWND>(wakeWindow), WM_NULL, 0, 0);
    else if (threadId)
        PostThreadMessageW(threadId, WM_NULL, 0, 0);
}

struct Dispatcher::Impl
{
    std::mutex mutex;
    HWND wakeWindow = nullptr;
    u32 thread = currentThreadId();

    void wake() noexcept
    {
        std::lock_guard lock(mutex);
        wakeThread(wakeWindow, thread);
    }
};

Dispatcher::Dispatcher() : m_impl(std::make_shared<Impl>())
{
    registerWakeClass();
    m_impl->wakeWindow = CreateWindowExW(0, L"VeloraDispatchQueue", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                         nullptr, nullptr);
}

Dispatcher::~Dispatcher()
{
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->wakeWindow)
        DestroyWindow(m_impl->wakeWindow);
    m_impl->wakeWindow = nullptr;
    m_impl->thread = 0;
}

bool Dispatcher::waitEvents(int timeoutMs)
{
    return dispatchThreadMessages(timeoutMs);
}

void Dispatcher::pump()
{
    dispatchThreadMessages(0);
}

void Dispatcher::wake() noexcept
{
    m_impl->wake();
}

std::function<void()> Dispatcher::wakeCallback() const
{
    return [state = m_impl] { state->wake(); };
}

bool Dispatcher::isOwnerThread() const noexcept
{
    return currentThreadId() == m_impl->thread;
}

} // namespace velora
