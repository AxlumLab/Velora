#include "../examples/custom_control/ValuePad.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/InputContext.hpp>
#include <velora/runtime/Framework.hpp>

using namespace velora;

namespace
{
int failures = 0;
void check(bool result, const char* message)
{
    if (!result)
    {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}

bool near(f64 a, f64 b)
{
    return std::abs(a - b) < 0.01;
}

void collectText(const std::vector<Command>& commands, std::vector<std::string>& text)
{
    for (const auto& command : commands)
        if (const auto* layer = std::get_if<LayerRef>(&command))
            collectText(layer->commands->commands, text);
        else if (const auto* label = std::get_if<TextCommand>(&command))
            text.push_back(label->value);
}

bool hasText(const SceneSnapshot& scene, std::string_view value)
{
    std::vector<std::string> text;
    collectText(scene.displayList->commands(), text);
    for (const auto& label : text)
        if (label == value)
            return true;
    return false;
}

class ProbePad final : public ValuePad
{
  public:
    std::vector<std::string> log;
    bool sawModifiers = false;
    void onPointerPressed(InputContext& context, PointerEventArgs& args) override
    {
        log.push_back("class");
        ValuePad::onPointerPressed(context, args);
    }
    void onKeyDown(InputContext& context, KeyEventArgs& args) override
    {
        sawModifiers = args.shift && args.ctrl;
        ValuePad::onKeyDown(context, args);
    }
};

void inputAndRendering()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto panel = ui.create(ElementKind::kStackPanel);
    ui.set(panel, ui.props().Margin, Thickness{20, 10, 0, 0});
    auto pad = createElement<ProbePad>();
    auto second = createElement<ValuePad>();
    const auto firstId = ui.insert(pad, panel);
    const auto secondId = ui.insert(second, panel);
    ui.set(firstId, ui.props().HorizontalAlignment, "Left");
    ui.set(secondId, ui.props().HorizontalAlignment, "Left");
    ui.set(firstId, ui.props().Content, "built-in fallback");
    ui.update(400, 250);
    const Box box = ui.bounds(firstId);
    check(near(box.x, 20) && near(box.y, 10) && near(box.width, 240) && near(box.height, 64),
          "custom measureOverride determines size in the existing layout tree");
    check(ui.tree().uiElement(firstId) == pad.get(), "custom input and paint use the hosted object");
    const auto initial = compositor.snapshot();
    check(hasText(initial, "Value 0") && !hasText(initial, "built-in fallback"),
          "virtual rendering replaces the built-in content painter");

    int ordinary = 0;
    bool localCoordinates = false, ancestorCoordinates = false;
    auto skipped = ui.subscribeEvent(firstId, ui.events().PointerPressed,
                                     [&](ElementId, RoutedEventArgs&) { ++ordinary; });
    auto observer = ui.subscribeEvent(
        firstId, ui.events().PointerPressed,
        [&](ElementId sender, RoutedEventArgs& args)
        {
            pad->log.push_back("instance");
            const auto* pointer = dynamic_cast<const PointerEventArgs*>(&args);
            localCoordinates = pointer && sender == firstId && pointer->originalSource == firstId &&
                               near(pointer->x, 60) && near(pointer->y, 5);
        },
        true);
    auto ancestor = ui.subscribeEvent(
        panel, ui.events().PointerPressed,
        [&](ElementId, RoutedEventArgs& args)
        {
            pad->log.push_back("ancestor");
            const auto* pointer = dynamic_cast<const PointerEventArgs*>(&args);
            ancestorCoordinates = pointer && near(pointer->windowX, 80) && near(pointer->x, 60);
        },
        true);
    ui.pointerDown(box.x + 60, box.y + 5);
    check(near(pad->value(), 25) && ui.focused() == firstId && ui.tree().pointerCapture() == firstId,
          "custom pointer handling updates its dependency property, focuses and captures");
    check(ordinary == 0 && localCoordinates && ancestorCoordinates &&
              pad->log == std::vector<std::string>{"class", "instance", "ancestor"},
          "class handling precedes instances, preserves coordinates and honors Handled");

    int focusEvents = 0, keyEvents = 0, toggles = 0;
    auto focused = ui.subscribeEvent(panel, ui.events().GotFocus, [&](ElementId, RoutedEventArgs& args)
                                     { focusEvents += args.originalSource == secondId; });
    auto keyed = ui.subscribeEvent(
        secondId, ui.events().KeyDown,
        [&](ElementId, RoutedEventArgs& args)
        {
            const auto* key = dynamic_cast<const KeyEventArgs*>(&args);
            keyEvents += key && key->key == Key::kRight && args.originalSource == secondId;
        },
        true);
    auto toggle = createElement<ToggleSwitchElement>();
    const auto toggleId = ui.insert(toggle, panel);
    auto toggled = ui.subscribeEvent(toggleId, ui.events().Toggled, [&](ElementId, RoutedEventArgs& args)
                                     { toggles += args.originalSource == toggleId; });
    ui.focus(secondId);
    ui.key(Key::kRight);
    toggle->setIsOn(true);
    check(focusEvents == 1 && keyEvents == 1 && toggles == 1 && near(second->value(), 1) &&
              near(pad->value(), 25),
          "pointer capture does not redirect focus, key or state events");
    ui.pointerMove(box.x + box.width + 25, box.y + 5);
    check(near(pad->value(), 100), "captured pointer movement outside the control reaches the override");
    ui.pointerUp(box.x + box.width + 25, box.y + 5);
    check(ui.tree().pointerCapture() == 0, "custom pointer release drops capture");
    ui.pointerMove(box.x + 5, box.y + 5);
    check(near(pad->value(), 100), "uncaptured movement no longer changes the value");

    ui.focus(firstId);
    ui.key(Key::kLeft, true, true);
    check(pad->sawModifiers && near(pad->value(), 90), "virtual keys receive modifiers and consume input");
    ui.set(secondId, ui.props().IsTabStop, false);
    ui.set(toggleId, ui.props().IsTabStop, false);
    ui.focus(0);
    check(ui.moveFocus(true) && ui.focused() == firstId, "custom controls opt into Tab navigation");

    pad->setValue(35);
    ui.update(400, 250);
    const auto changed = compositor.snapshot();
    check(hasText(changed, "Value 35") && !hasText(initial, "Value 35"),
          "dependency-property invalidation repaints while old snapshots remain immutable");
    ui.set(panel, ui.props().IsEnabled, false);
    ui.key(Key::kRight);
    ui.pointerDown(box.x + 120, box.y + 5);
    check(near(pad->value(), 35) && ui.tree().pointerCapture() == 0 && !ui.moveFocus(true),
          "disabled ancestry blocks custom input and Tab navigation");

    ui.tree().remove(panel);
    check(!pad->isInTree() && !second->isInTree() && ui.focused() == 0,
          "custom controls unload through the existing tree lifecycle");
    pad->setValue(55);
    ui.update(400, 250);
    check(!hasText(compositor.snapshot(), "Value 55"), "detached controls cannot repaint their former host");
}

void callbackRemoval()
{
    struct RetiringPad final : ValuePad
    {
        std::function<void()> retire;
        bool* destroyed = nullptr;
        bool* survived = nullptr;
        ~RetiringPad() override
        {
            *destroyed = true;
        }
        void onPointerPressed(InputContext&, PointerEventArgs& args) override
        {
            retire();
            *survived = !*destroyed;
            args.handled = true;
        }
    };
    Compositor compositor;
    Framework ui(compositor);
    bool destroyed = false, survived = false;
    auto pad = createElement<RetiringPad>();
    const auto id = ui.insert(pad);
    pad->destroyed = &destroyed;
    pad->survived = &survived;
    pad->retire = [&]
    {
        ui.tree().remove(id);
        pad.reset();
    };
    int instances = 0;
    auto subscription = ui.subscribeEvent(
        id, ui.events().PointerPressed, [&](ElementId, RoutedEventArgs&) { ++instances; }, true);
    ui.update(300, 120);
    ui.pointerDown(20, 20);
    check(survived && destroyed && instances == 0 && !ui.tree().contains(id),
          "class callbacks may remove the last tree and caller references without use-after-free");
    subscription.reset();
}
} // namespace

int main()
{
    inputAndRendering();
    callbackRemoval();
    std::printf("custom control: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
