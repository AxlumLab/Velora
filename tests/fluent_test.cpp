// R11.1: the Fluent framework package. Velora.Fluent.vpak (the WinUI 2.3 Button/TextBox
// dictionaries compiled by velorac) is mounted in the framework namespace; pages merge it by
// ms-appx-framework URI and get the WinUI templates and visual states; an application's own implicit
// style and resource keys override the framework's.
#include "FirstBatch.g.hpp"
#include "Overridden.g.hpp"
#include "Plain.g.hpp"
#include "SecondBatch.g.hpp"

#include <cstdio>
#include <string>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/Automation.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/FluentPackage.hpp>
#include <velora/runtime/Framework.hpp>

using namespace velora;

namespace
{
int g_failures = 0;

void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: velora_fluent_test <Velora.Fluent.vpak>\n");
        return 2;
    }
    check(!mountFluent("missing.vpak").empty(), "a missing package is refused");
    if (const auto error = mountFluent(argv[1]); !error.empty())
    {
        std::printf("FAIL: %s\n", error.c_str());
        return 1;
    }

    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    FluentTest::Plain plain;
    FluentTest::Plain_component::Names names;
    const auto load = FluentTest::Plain_component::initializeComponent(ui, plain, names);
    check(load.ok, "a page merging the framework dictionaries loads");
    if (!load.ok)
    {
        std::printf("  %s\n", load.error.c_str());
        return 1;
    }
    const ElementId button = ui.tree().element(*names.Action);
    const ElementId presenter = ui.templateChild(button, "ContentPresenter");
    check(presenter != 0, "the WinUI Button template is instantiated from the package");
    check(names.Action->getValue(*p.CornerRadius).asDouble() == 2 &&
              ui.tree().property(presenter, *p.Content).asString() == "Fluent",
          "DefaultButtonStyle setters (ControlCornerRadius) and the template's ContentPresenter apply");
    check(ui.currentState(button, "CommonStates") == "Normal", "the template's CommonStates are active");

    const auto pointerOver = ui.resources().find(button, "ButtonBackgroundPointerOver");
    ui.goToState(button, "PointerOver");
    ui.advance(0.01);
    check(pointerOver && ui.tree().property(presenter, *p.Background) == *pointerOver,
          "a discrete key frame switches the part's Background to the state's resource");
    ui.goToState(button, "Normal");
    const PropertyValue light = ui.tree().property(presenter, *p.Background);
    ui.setTheme(Theme::kDark);
    check(ui.tree().property(presenter, *p.Background) != light,
          "the framework ThemeResources follow the theme");
    ui.setTheme(Theme::kLight);
    check(ui.templateChild(ui.tree().element(*names.Input), "BorderElement") != 0 ||
              ui.templateChild(ui.tree().element(*names.Input), "ContentElement") != 0 ||
              names.Input->getValue(*p.Padding) != PropertyValue(Thickness{}),
          "the WinUI TextBox style applies too");

    // The application overrides the framework without touching control code.
    Compositor overriddenCompositor;
    Framework overridden(overriddenCompositor);
    FluentTest::Overridden controller;
    FluentTest::Overridden_component::Names overriddenNames;
    const auto second =
        FluentTest::Overridden_component::initializeComponent(overridden, controller, overriddenNames);
    check(second.ok, "a page overriding the framework style loads");
    if (second.ok)
    {
        const ElementId action = overridden.tree().element(*overriddenNames.Action);
        const ElementId part = overridden.templateChild(action, "ContentPresenter");
        check(overriddenNames.Action->getValue(*p.Padding) == PropertyValue(Thickness{20, 20, 20, 20}) &&
                  part != 0,
              "the app's implicit style wins and still builds on DefaultButtonStyle (template kept)");
        overridden.goToState(action, "PointerOver");
        overridden.advance(0.01);
        check(overridden.tree().property(part, *p.Background).asInt() == 0xFF123456,
              "an app resource key overrides the framework's inside the framework template");
    }

    // R11.3: the first control batch through the whole chain, on one page.
    {
        Compositor batchCompositor;
        Framework ui3(batchCompositor);
        FluentTest::FirstBatch batch;
        FluentTest::FirstBatch_component::Names n;
        const auto loaded = FluentTest::FirstBatch_component::initializeComponent(ui3, batch, n);
        check(loaded.ok, "the first-batch page loads");
        if (!loaded.ok)
            std::printf("  %s\n", loaded.error.c_str());
        else
        {
            ui3.update(800, 400);
            const auto id = [&](const std::shared_ptr<UIElement>& e) { return ui3.tree().element(*e); };
            const auto click = [&](ElementId e)
            {
                const Box b = ui3.bounds(e);
                ui3.pointerDown(b.x + b.width / 2, b.y + b.height / 2);
                ui3.pointerUp(b.x + b.width / 2, b.y + b.height / 2);
                ui3.update(800, 400);
            };
            // Grid and StackPanel layout; TextBlock measures its text.
            const Box caption = ui3.bounds(id(n.Caption)), input = ui3.bounds(id(n.Input)),
                      card = ui3.bounds(id(n.Card)), scroller = ui3.bounds(id(n.Scroller));
            check(caption.width > 0 && input.x >= caption.x + caption.width && card.y > caption.y &&
                      scroller.y >= card.y + card.height && scroller.height == 80,
                  "Grid rows and StackPanel stacking place the controls; TextBlock measures its text");
            // TextBox: the Fluent style, focus by pointer, text input, TextChanged.
            check(n.Input->getValue(*p.Padding) != PropertyValue(Thickness{}),
                  "the WinUI TextBox style applies");
            click(id(n.Input));
            ui3.textInput("hi");
            check(ui3.focused() == id(n.Input) && n.Input->getValue(*p.Text).asString() == "hi" &&
                      batch.textChanged == 1,
                  "TextBox takes focus by pointer, edits text and raises TextChanged");
            // Tab navigation: Input -> Agree -> Submit -> (wraps) Input; Shift+Tab back; disabled skipped.
            ui3.key(Key::kTab);
            const ElementId afterInput = ui3.focused();
            ui3.key(Key::kTab);
            const ElementId afterAgree = ui3.focused();
            ui3.key(Key::kTab);
            const ElementId wrapped = ui3.focused();
            ui3.key(Key::kTab, true);
            const ElementId back = ui3.focused();
            check(afterInput == id(n.Agree) && afterAgree == id(n.Submit) && wrapped == id(n.Input) &&
                      back == id(n.Submit),
                  "Tab and Shift+Tab move focus through the tab stops in tree order, wrapping");
            ui3.set(id(n.Agree), p.IsEnabled, PropertyValue(false));
            ui3.key(Key::kTab); // Submit -> wraps to Input
            ui3.key(Key::kTab); // Input -> skips the disabled CheckBox
            check(ui3.focused() == id(n.Submit), "a disabled control is not a tab stop");
            ui3.set(id(n.Agree), p.IsEnabled, PropertyValue(true));
            // CheckBox: pointer toggles IsChecked and raises Checked.
            click(id(n.Agree));
            check(n.Agree->getValue(*p.IsChecked).asBool() && batch.checkedCount == 1,
                  "CheckBox toggles and raises Checked");
            // Button: the WinUI template and Click.
            click(id(n.Submit));
            check(ui3.templateChild(id(n.Submit), "ContentPresenter") != 0 && batch.clicks == 1,
                  "the templated Button raises Click");
            // Border: a framework ThemeResource that follows the theme.
            const PropertyValue light = n.Card->getValue(*p.Background);
            ui3.setTheme(Theme::kDark);
            check(n.Card->getValue(*p.Background) != light, "Border background follows the theme");
            ui3.setTheme(Theme::kLight);
            // Automation (R10.5): the tree and the patterns.
            const AutomationNode tree = automationTree(ui3, id(n.Layout));
            const auto findNode = [&](auto&& self, const AutomationNode& node,
                                      ElementId e) -> const AutomationNode*
            {
                if (node.element == e)
                    return &node;
                for (const auto& c : node.children)
                    if (const auto* found = self(self, c, e))
                        return found;
                return nullptr;
            };
            const auto* submitNode = findNode(findNode, tree, id(n.Submit));
            const auto* inputNode = findNode(findNode, tree, id(n.Input));
            check(submitNode && submitNode->type == AutomationControlType::kButton &&
                      submitNode->name == "Submit" && submitNode->children.empty() && inputNode &&
                      inputNode->type == AutomationControlType::kEdit && inputNode->text == std::string("hi"),
                  "the automation tree names controls and hides template parts");
            const int clicksBefore = batch.clicks;
            check(automationInvoke(ui3, id(n.Submit)) && batch.clicks == clicksBefore + 1,
                  "Invoke clicks the Button through its keyboard activation");
            const bool agreed = n.Agree->getValue(*p.IsChecked).asBool();
            check(automationToggle(ui3, id(n.Agree)) && n.Agree->getValue(*p.IsChecked).asBool() != agreed,
                  "Toggle flips the CheckBox");
            check(automationSetValue(ui3, id(n.Input), "typed") &&
                      n.Input->getValue(*p.Text).asString() == "typed" && !automationInvoke(ui3, id(n.Input)),
                  "Value sets TextBox text; a TextBox does not support Invoke");
            ui3.set(id(n.Submit), p.IsEnabled, PropertyValue(false));
            check(!automationInvoke(ui3, id(n.Submit)), "a disabled control refuses its patterns");
            ui3.set(id(n.Submit), p.IsEnabled, PropertyValue(true));
            // ScrollViewer: the wheel scrolls its content and raises ViewChanged.
            ui3.wheel(scroller.x + 10, scroller.y + 10, 40);
            ui3.update(800, 400);
            check(batch.viewChanged >= 1, "ScrollViewer scrolls by wheel and raises ViewChanged");
        }
    }

    // R11.4: the second batch the runtime has (ListView items are covered by velora_vxbf_test).
    {
        Compositor c4;
        Framework ui4(c4);
        FluentTest::SecondBatch batch;
        FluentTest::SecondBatch_component::Names n;
        const auto loaded = FluentTest::SecondBatch_component::initializeComponent(ui4, batch, n);
        check(loaded.ok, "the second-batch page loads");
        if (loaded.ok)
        {
            ui4.update(800, 400);
            const auto id = [&](const std::shared_ptr<UIElement>& e) { return ui4.tree().element(*e); };
            const auto press = [&](float x, float y)
            {
                ui4.pointerDown(x, y);
                ui4.pointerUp(x, y);
                ui4.update(800, 400);
            };
            const auto centre = [&](ElementId e)
            {
                const Box b = ui4.bounds(e);
                return std::pair{b.x + b.width / 2, b.y + b.height / 2};
            };
            // TabIndex orders tab stops (Large 0, Volume 1, Wifi 2); IsTabStop="False" skips Small.
            std::vector<ElementId> order;
            for (int i = 0; i < 4; ++i)
            {
                ui4.key(Key::kTab);
                order.push_back(ui4.focused());
            }
            check(order == std::vector<ElementId>{id(n.Large), id(n.Volume), id(n.Wifi), id(n.Large)},
                  "TabIndex orders Tab navigation and IsTabStop=False leaves a control out");
            const auto [wx, wy] = centre(id(n.Wifi));
            press(wx, wy);
            check(n.Wifi->getValue(*p.IsOn).asBool() && batch.toggled == 1,
                  "ToggleSwitch toggles and raises Toggled");
            const Box slider = ui4.bounds(id(n.Volume));
            press(slider.x + slider.width * 0.75f, slider.y + slider.height / 2);
            const double volume = n.Volume->getValue(*p.Value).asDouble();
            check(volume > 50 && volume <= 100 && batch.valueChanged >= 1,
                  "a pointer on the Slider track moves Value and raises ValueChanged");
            const auto [lx, ly] = centre(id(n.Large));
            press(lx, ly);
            check(n.Large->getValue(*p.IsChecked).asBool() && !n.Small->getValue(*p.IsChecked).asBool() &&
                      batch.large == 1,
                  "RadioButtons of one group are exclusive");
            check(n.Progress->getValue(*p.Value).asDouble() == 10,
                  "ProgressBar coerces Value into its range");
            check(automationSetRangeValue(ui4, id(n.Volume), 250) &&
                      n.Volume->getValue(*p.Value).asDouble() == 100 &&
                      automationTree(ui4, ui4.tree().parent(id(n.Volume))).children.size() >= 4,
                  "RangeValue sets the Slider through its range coercion");
        }
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
