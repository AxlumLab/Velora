// N04: WinUI-like Application startup with XamlApplication/XamlWindow.
// Minimal user code: Application handles all initialization (COM, graphics, text, compositor).
#include <cstdio>
#include <velora/application/XamlApplication.hpp>
#include <velora/application/XamlWindow.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/text/TextService.hpp>
#include <windows.h>

using namespace velora;

namespace
{

int g_failures = 0;

void check(bool passed, const char* name)
{
    std::printf("N04 %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++g_failures;
}

// Simple XamlWindow subclass that builds UI manually.
class TestWindow : public XamlWindow
{
public:
    explicit TestWindow(XamlApplication& app) : XamlWindow(app)
    {
        // Create the window.
        WindowOptions options;
        options.title = "Velora N04 Test";
        options.width = 400;
        options.height = 300;
        if (!Create(options))
        {
            check(false, "window created");
            return;
        }
        check(true, "window created");

        // Build a simple UI.
        Framework& ui = GetFramework();
        const auto& p = ui.props();

        m_page = ui.create(ElementKind::kPage);
        const ElementId stack = ui.create(ElementKind::kStackPanel, m_page);
        ui.set(stack, p.Margin, Thickness{20, 20, 20, 20});
        ui.set(stack, p.Spacing, 10.0);

        m_title = ui.create(ElementKind::kTextBlock, stack);
        ui.set(m_title, p.Text, "Hello N04");
        ui.set(m_title, p.FontSize, 24.0);

        m_button = ui.create(ElementKind::kButton, stack);
        ui.set(m_button, p.Content, std::string("Click Me"));

        m_greeting = ui.create(ElementKind::kTextBlock, stack);
        ui.set(m_greeting, p.FontSize, 16.0);

        // Bind button click.
        ui.on(m_button, ui.events().Click, [this, &ui, &p]()
        {
            ui.set(m_greeting, p.Text, "Button clicked!");
            ++m_clickCount;
        });
    }

    int GetClickCount() const { return m_clickCount; }
    ElementId GetButton() const { return m_button; }
    ElementId GetGreeting() const { return m_greeting; }

private:
    ElementId m_page = 0;
    ElementId m_title = 0;
    ElementId m_button = 0;
    ElementId m_greeting = 0;
    int m_clickCount = 0;
};

class TestApp : public XamlApplication
{
public:
    void OnLaunched() override
    {
        check(graphics().isReady(), "graphics initialized in OnLaunched");
        check(textService().isReady(), "text service initialized in OnLaunched");

        // Create window through the wrapper.
        m_window = std::make_unique<TestWindow>(*this);
        m_window->Show();

        // Perform first update to layout the UI.
        m_window->Update();

        // Simulate a click.
        Framework& ui = m_window->GetFramework();
        const Box buttonBounds = ui.bounds(m_window->GetButton());

        check(buttonBounds.width > 0 && buttonBounds.height > 0, "button has valid bounds after layout");

        ui.pointerDown(buttonBounds.x + 2, buttonBounds.y + 2);
        ui.pointerUp(buttonBounds.x + 2, buttonBounds.y + 2);

        check(m_window->GetClickCount() == 1, "button click handled");

        const auto& greetingText = ui.tree().property(m_window->GetGreeting(), *ui.props().Text);
        check(greetingText.isString() && greetingText.asString() == "Button clicked!",
              "UI updated after click");

        // Exit immediately for testing.
        application().exit(0);
    }

    TestWindow* GetWindow() { return m_window.get(); }

private:
    std::unique_ptr<TestWindow> m_window;
};

void testApplicationStartup()
{
    TestApp app;
    const i32 exitCode = app.Run();
    check(exitCode == 0, "Run returns exit code 0");
}

} // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    testApplicationStartup();
    std::printf("N04 exit=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
