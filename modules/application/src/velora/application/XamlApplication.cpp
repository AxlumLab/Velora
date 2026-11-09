#include "velora/application/XamlApplication.hpp"

#include <algorithm>
#include <velora/application/XamlWindow.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/text/TextService.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace velora
{

XamlApplication::XamlApplication() = default;

XamlApplication::~XamlApplication() = default;

bool XamlApplication::initialize()
{
    if (m_initialized)
        return true;

#ifdef _WIN32
    // Initialize COM for this thread (TSF text input needs STA).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif

    // Initialize graphics device.
    m_graphics = std::make_unique<GraphicsDevice>();
    if (!m_graphics->initialize() || !m_graphics->isReady())
        return false;

    // Initialize text service.
    m_textService = std::make_unique<TextService>();
    if (!m_textService->initialize() || !m_textService->isReady())
        return false;

    // No Framework or Compositor here: each XamlWindow creates its own XamlRoot.
    m_initialized = true;
    return true;
}

i32 XamlApplication::Run()
{
    if (!initialize())
        return -1;

    // Set up frame callback to update all XAML windows.
    m_app.setFrameCallback(
        [this]()
        {
            // Each window updates its own layout and frame scheduler.
            updateWindows();
            // Check if any window has pending work.
            for (XamlWindow* window : m_xamlWindows)
            {
                if (window && window->HasPendingWork())
                    return true;
            }
            return false;
        });

    // Set up launch callback that will be called by Application::run().
    m_app.setLaunchHandler([this](Application&) { OnLaunched(); });

    // Run the event loop.
    const i32 exitCode = m_app.run();

#ifdef _WIN32
    CoUninitialize();
#endif

    return exitCode;
}

void XamlApplication::registerWindow(XamlWindow* window)
{
    if (window)
        m_xamlWindows.push_back(window);
}

void XamlApplication::unregisterWindow(XamlWindow* window)
{
    m_xamlWindows.erase(std::remove(m_xamlWindows.begin(), m_xamlWindows.end(), window), m_xamlWindows.end());
}

void XamlApplication::updateWindows()
{
    for (XamlWindow* window : m_xamlWindows)
    {
        if (window && !window->IsClosed())
            window->Update();
    }
}

} // namespace velora
