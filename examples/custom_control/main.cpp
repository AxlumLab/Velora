#include "ValuePad.hpp"

#include <memory>
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
        const auto panel = ui.create(ElementKind::kStackPanel);
        ui.set(panel, ui.props().Margin, Thickness{24, 24, 24, 24});
        const auto hint = ui.create(ElementKind::kTextBlock, panel);
        ui.set(hint, ui.props().Text, "Drag or Tab, then arrows. Shift: step 10.");
        ui.set(hint, ui.props().Margin, Thickness{0, 0, 0, 16});
        auto pad = createElement<ValuePad>();
        pad->setValue(30);
        ui.insert(pad, panel);
        SetRoot(panel);
    }
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
