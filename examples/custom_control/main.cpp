#include "SourcePage.g.hpp"
#include "ValuePad.hpp"

#include <memory>
#include <stdexcept>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/runtime/Framework.hpp>
#include <windows.h>

using namespace velora;

class CustomWindow final : public XamlWindow
{
  public:
    explicit CustomWindow(XamlApplication& app) : XamlWindow(app)
    {
        WindowOptions options;
        options.title = "Velora source control";
        options.width = 480;
        options.height = 240;
        if (!Create(options))
            return;
        auto& ui = GetFramework();
        if (const auto error = ValuePad::registerType(ui); !error.empty())
            throw std::runtime_error(error);
        Demo::SourcePage_component::Names names;
        const auto loaded = Demo::SourcePage_component::initializeComponent(ui, m_controller, names);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        SetRoot(ui.tree().element(*loaded.root));
    }

  private:
    Demo::SourcePage m_controller;
};

class CustomApp final : public XamlApplication
{
  public:
    void OnLaunched() override
    {
        m_window = std::make_unique<CustomWindow>(*this);
        m_window->Show();
    }

  private:
    std::unique_ptr<CustomWindow> m_window;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    CustomApp app;
    return app.Run();
}
