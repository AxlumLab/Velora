// N04: WinUI-like Application startup with compiled XAML, loaded from its .vxbf (R07.6).
// User writes minimal code: Application handles all initialization.
#include "MainWindow.g.hpp"

#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <windows.h>

using namespace velora;

// Wrapper that integrates the XAML view into a XamlWindow.
class MainWindowWrapper : public XamlWindow
{
public:
    explicit MainWindowWrapper(XamlApplication& app) : XamlWindow(app)
    {
        // Create the native window.
        WindowOptions options;
        options.title = "Velora N04";
        options.width = 400;
        options.height = 300;
        if (!Create(options))
            return; // no window: Run() returns once nothing is open

        // Load the compiled page into this window's root; MainWindow.vxbf is embedded in the
        // executable by the generated code-behind.
        const auto load =
            N04Example::MainWindow_component::initializeComponent(GetFramework(), m_controller, m_names);
        if (!load.ok)
        {
            OutputDebugStringA(load.error.c_str());
            Close();
            return;
        }
        SetRoot(GetFramework().tree().element(*load.root));
    }

private:
    N04Example::MainWindow m_controller;
    N04Example::MainWindow_component::Names m_names;
};

class MyApp : public XamlApplication
{
public:
    void OnLaunched() override
    {
        // Create and show the main window.
        m_window = std::make_unique<MainWindowWrapper>(*this);
        m_window->Show();
    }

private:
    std::unique_ptr<MainWindowWrapper> m_window;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    MyApp app;
    return app.Run();
}
