#pragma once

#include <memory>
#include <vector>
#include <velora/application/Application.hpp>
#include <velora/globals.hpp>

namespace velora
{

class GraphicsDevice;
class TextService;
class XamlWindow;

// WinUI-like Application wrapper that initializes the services all windows share — the graphics
// device and the text service — and runs the event loop. Users derive from this and override
// OnLaunched() to create windows. UI state is not shared: every XamlWindow owns its own XamlRoot
// (R04), so the application holds no Framework or Compositor.
class XamlApplication
{
public:
    XamlApplication();
    virtual ~XamlApplication();

    XamlApplication(const XamlApplication&) = delete;
    XamlApplication& operator=(const XamlApplication&) = delete;

    // Called after initialization completes. Override to create and show windows.
    virtual void OnLaunched() {}

    // Initializes graphics and text, calls OnLaunched(), then runs the event loop.
    [[nodiscard]] i32 Run();

    // Access to shared services.
    [[nodiscard]] Application& application() noexcept { return m_app; }
    [[nodiscard]] GraphicsDevice& graphics() noexcept { return *m_graphics; }
    [[nodiscard]] TextService& textService() noexcept { return *m_textService; }
    [[nodiscard]] bool hasTextService() const noexcept { return m_textService != nullptr; }

    // Register a XamlWindow for automatic frame scheduling.
    void registerWindow(XamlWindow* window);
    void unregisterWindow(XamlWindow* window);

protected:
    [[nodiscard]] bool initialize();
    void updateWindows();

private:
    Application m_app;
    std::unique_ptr<GraphicsDevice> m_graphics;
    std::unique_ptr<TextService> m_textService;
    std::vector<XamlWindow*> m_xamlWindows;
    bool m_initialized = false;
};

} // namespace velora
