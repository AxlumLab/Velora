#include "FluentPage.g.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <velora/fluent/FluentPackage.hpp>
#include <velora/fluent/PersonPicture.hpp>
#include <windows.h>

using namespace velora;
class FluentWindow final : public XamlWindow
{
  public:
    explicit FluentWindow(XamlApplication& app) : XamlWindow(app)
    {
        std::array<wchar_t, 32768> path{};
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length || length == path.size())
            throw std::runtime_error("cannot locate executable");
        const auto package = std::filesystem::path(path.data()).parent_path() / "Velora.Fluent.vpak";
        if (const auto error = mountFluent(package); !error.empty())
            throw std::runtime_error(error);
        WindowOptions options;
        options.title = "Velora Fluent controls";
        options.width = 600;
        options.height = 330;
        if (!Create(options))
            return;
        auto& ui = GetFramework();
        if (const auto error = PersonPicture::registerType(ui); !error.empty())
            throw std::runtime_error(error);
        Demo::FluentPage_component::Names names;
        const auto loaded = Demo::FluentPage_component::initializeComponent(ui, m_controller, names);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        m_controller.setPicture(std::dynamic_pointer_cast<PersonPicture>(names.Avatar));
        m_page = ui.tree().element(*loaded.root);
        SetRoot(m_page);
    }
    ~FluentWindow() override
    {
        if (m_page)
        {
            SetRoot(0);
            GetFramework().tree().remove(m_page);
        }
    }

  private:
    Demo::FluentPage m_controller;
    ElementId m_page = 0;
};

class FluentApp final : public XamlApplication
{
  public:
    void OnLaunched() override
    {
        m_window = std::make_unique<FluentWindow>(*this);
        m_window->Show();
    }

  private:
    std::unique_ptr<FluentWindow> m_window;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    FluentApp app;
    return app.Run();
}
