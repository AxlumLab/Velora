#pragma once

#include <memory>
#include <string>
#include <velora/application/AppWindow.hpp>
#include <velora/globals.hpp>

namespace velora
{

class XamlApplication;
class XamlHost;
class XamlRoot;
class Framework;

// XAML Window that wraps AppWindow + XamlHost and owns the window's XamlRoot (R04): its own
// layout, input, focus, theme, composition tree and resource context. Nothing of it is shared
// with other windows.
class XamlWindow
{
public:
    explicit XamlWindow(XamlApplication& app);
    virtual ~XamlWindow();

    XamlWindow(const XamlWindow&) = delete;
    XamlWindow& operator=(const XamlWindow&) = delete;

    // Create the native window and attach the XAML host.
    [[nodiscard]] bool Create(const WindowOptions& options);

    // Show the window.
    void Show();

    // Close the window.
    void Close();
    // Mounts a top-level element of this window's root as its Content (XamlRoot::setContent). The
    // previous content is unmounted, not destroyed.
    bool SetRoot(u64 element);

    // Check if window is closed.
    [[nodiscard]] bool IsClosed() const;

    // Update the window (layout + present frame).
    void Update();

    // Check if frame scheduling has pending work.
    [[nodiscard]] bool HasPendingWork() const;

    // Access to the underlying AppWindow.
    [[nodiscard]] AppWindow* GetAppWindow() noexcept
    {
        return m_window;
    }

    // This window's root (created on first use), and its UI context for building UI.
    [[nodiscard]] XamlRoot& GetXamlRoot();
    [[nodiscard]] Framework& GetFramework();

protected:
    XamlApplication& m_app;
    AppWindow* m_window = nullptr;
    // Declared before the host: the host references the root's framework and is destroyed first.
    std::unique_ptr<XamlRoot> m_root;
    std::unique_ptr<XamlHost> m_host;

private:
    void syncHostState();
};

} // namespace velora
