// Installed-package consumer: HAL scaffolding types link, and a velorac-generated XAML page
// runs headless against velora::framework.
#include "Hello.g.hpp"

#include <cstdio>
#include <stdexcept>
#include <velora/application/Application.hpp>
#include <velora/composition/Compositor.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/text/TextService.hpp>

int main()
{
    // N02 implements these; until then they must report "not ready", never fake success.
    velora::GraphicsDevice graphics;
    velora::TextService text;
    if (graphics.isReady() || text.isReady())
    {
        std::puts("consumer: HAL scaffolding unexpectedly reports ready");
        return 1;
    }

    velora::Compositor compositor;
    velora::Framework ui(compositor);
    Consumer::Hello controller;

    Consumer::Hello_component::Names names;
    const auto loaded = Consumer::Hello_component::initializeComponent(ui, controller, names);
    if (!loaded.ok)
        throw std::runtime_error(loaded.error);

    ui.update(400, 200);
    controller.setName("Installed Velora");
    const auto nameId = ui.tree().element(*names.Greeting);
    const auto& textValue = ui.tree().property(nameId, *ui.props().Text);
    const auto buttonId = ui.tree().element(*names.Greet);
    const auto button = ui.bounds(buttonId);
    ui.pointerDown(button.x + 2, button.y + 2);
    ui.pointerUp(button.x + 2, button.y + 2);
    const bool ok =
        textValue.isString() && textValue.asString() == "Installed Velora" && controller.greetings() == 1;
    std::printf("consumer: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
