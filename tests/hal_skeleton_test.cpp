// HAL objects before initialization: nothing is ready and nothing pretends to work.
#include <cassert>
#include <type_traits>
#include <velora/application/Application.hpp>
#include <velora/graphics/DisplayList.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/text/TextService.hpp>

int main()
{
    static_assert(!std::is_copy_constructible_v<velora::GraphicsDevice>);
    static_assert(!std::is_copy_constructible_v<velora::GraphicsSurface>);
    static_assert(!std::is_copy_constructible_v<velora::AppWindow>);
    velora::GraphicsDevice graphics;
    velora::TextService text;
    velora::GraphicsSurface surface;
    velora::Application app;
    velora::TextMetrics metrics;
    assert(!graphics.isReady() && graphics.getBackend() == velora::GraphicsBackend::kUnavailable);
    assert(!surface.isAttached());
    assert(!surface.draw(velora::DisplayList(std::vector<velora::Command>{})) &&
           !surface.lastError().empty());
    assert(!surface.attach(graphics, nullptr, {})); // the device is not initialized
    assert(!text.isReady());
    assert(!text.measureText("hello", velora::FontDescriptor{}, metrics)); // not initialized
    assert(app.getOpenWindowCount() == 0 && app.run() == 0);               // no windows: returns at once
#if defined(_WIN32)
    assert(text.initialize() && text.measureText("hello", velora::FontDescriptor{}, metrics) &&
           metrics.widthDips > 0);
#else
    assert(!graphics.initialize() && !text.initialize());
#endif
    return 0;
}
