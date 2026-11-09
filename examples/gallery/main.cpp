// Core control gallery, compiled to .vxbf and hosted by Direct2D/DirectWrite on Windows.
#include "GalleryPage.g.hpp"

#include <memory>
#include <stdexcept>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <velora/runtime/Framework.hpp>
#include <windows.h>

using namespace velora;

class GalleryWindow : public XamlWindow
{
public:
    explicit GalleryWindow(XamlApplication& app) : XamlWindow(app)
    {
        WindowOptions options;
        options.title = "Velora Gallery";
        options.width = 960;
        options.height = 720;
        if (!Create(options))
            return; // no window: Run() returns once nothing is open

        Gallery::GalleryPage_component::Names names;
        const auto loaded = Gallery::GalleryPage_component::initializeComponent(GetFramework(), m_controller, names);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        m_root = GetFramework().tree().element(*loaded.root);
        if (!SetRoot(m_root))
            throw std::runtime_error("Could not mount the gallery page");
    }

    ~GalleryWindow()
    {
        // Unbind the view while its controller and models are still alive.
        if (m_root)
        {
            SetRoot(0);
            GetFramework().tree().remove(m_root);
        }
    }

private:
    Gallery::GalleryPage m_controller;
    ElementId m_root = 0;
};

class GalleryApp : public XamlApplication
{
public:
    void OnLaunched() override
    {
        m_window = std::make_unique<GalleryWindow>(*this);
        m_window->Show();
    }

private:
    std::unique_ptr<GalleryWindow> m_window;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    GalleryApp app;
    return app.Run();
}
