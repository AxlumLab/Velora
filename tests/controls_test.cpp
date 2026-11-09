// Toggle, range and Canvas controls: layout, pointer and keyboard activation, events,
// RadioButton groups, range coercion, Canvas placement and ordinary C++ element ownership.
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/globals.hpp>
#include <velora/reflect/Accessors.hpp>
#include <velora/runtime/Framework.hpp>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("controls %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

bool near(double a, double b)
{
    return std::fabs(a - b) < 0.01;
}

// 10 DIPs per character.
float fixedWidth(std::string_view text, float)
{
    return static_cast<float>(text.size()) * 10.0f;
}

struct Fixture
{
    Compositor compositor;
    Framework ui{compositor, fixedWidth};
    const Framework::Properties& p = ui.props();
    ElementId page = ui.create(ElementKind::kPage);
    ElementId stack = ui.create(ElementKind::kStackPanel, page);
    std::vector<std::string> log;

    ElementId add(ElementKind kind, ElementId parent = 0)
    {
        return ui.create(kind, parent ? parent : stack);
    }
    // Records events as "<name>:<element>".
    void watch(ElementId e, const EventDescriptor* event, const char* name)
    {
        ui.on(e, event, [this, e, name] { log.push_back(std::string(name) + ":" + std::to_string(e)); });
    }
    void click(ElementId e)
    {
        const Box b = ui.bounds(e);
        ui.pointerDown(b.x + 5, b.y + b.height / 2);
        ui.pointerUp(b.x + 5, b.y + b.height / 2);
        ui.update(400, 600);
    }
    bool flag(ElementId e, const PropertyDescriptor* d) const
    {
        return ui.tree().property(e, *d).asBool();
    }
    double value(ElementId e) const
    {
        return ui.tree().property(e, *p.Value).asDouble();
    }
};

void checkBox()
{
    Fixture f;
    const auto box = f.add(ElementKind::kCheckBox);
    f.ui.set(box, f.p.Content, PropertyValue(std::string("Remember")));
    f.ui.update(400, 600);
    const Box b = f.ui.bounds(box);
    check(near(b.width, 20 + 8 + 80) && near(b.height, 32), "CheckBox measures glyph, gap and text");
    f.watch(box, f.ui.events().Checked, "checked");
    f.watch(box, f.ui.events().Unchecked, "unchecked");
    f.watch(box, f.ui.events().Click, "click");
    f.click(box);
    const auto id = std::to_string(box);
    check(f.flag(box, f.p.IsChecked) && f.log == std::vector<std::string>{"checked:" + id, "click:" + id},
          "a click checks, raising Checked before Click");
    f.click(box);
    check(!f.flag(box, f.p.IsChecked) && f.log.size() == 4 && f.log[2] == "unchecked:" + id,
          "a second click unchecks");
    f.ui.set(box, f.p.IsChecked, PropertyValue(true));
    f.ui.set(box, f.p.IsChecked, PropertyValue(true));
    check(f.log.size() == 5 && f.log[4] == "checked:" + id, "a programmatic change raises Checked once");
    f.ui.set(box, f.p.IsEnabled, PropertyValue(false));
    f.click(box);
    check(f.flag(box, f.p.IsChecked) && f.log.size() == 5, "a disabled CheckBox ignores clicks");
    f.ui.set(box, f.p.IsEnabled, PropertyValue(true));
    f.click(box); // unchecks it and takes focus
    f.ui.key(Key::kEnter);
    check(f.ui.focused() == box && f.flag(box, f.p.IsChecked), "Enter toggles the focused CheckBox");
}

void radioGroups()
{
    Fixture f;
    const auto a = f.add(ElementKind::kRadioButton), b = f.add(ElementKind::kRadioButton);
    const auto other = f.add(ElementKind::kStackPanel);
    const auto c = f.add(ElementKind::kRadioButton, other);
    // A named group spans parents within the page.
    const auto n1 = f.add(ElementKind::kRadioButton), n2 = f.add(ElementKind::kRadioButton, other);
    f.ui.set(n1, f.p.GroupName, PropertyValue(std::string("Size")));
    f.ui.set(n2, f.p.GroupName, PropertyValue(std::string("Size")));
    f.ui.update(400, 600);
    f.click(a);
    f.click(c);
    check(f.flag(a, f.p.IsChecked) && f.flag(c, f.p.IsChecked), "unnamed groups are per parent");
    f.watch(a, f.ui.events().Unchecked, "unchecked");
    f.click(b);
    check(!f.flag(a, f.p.IsChecked) && f.flag(b, f.p.IsChecked) && f.flag(c, f.p.IsChecked) &&
              f.log == std::vector<std::string>{"unchecked:" + std::to_string(a)},
          "checking one sibling unchecks the other with Unchecked");
    f.click(b);
    check(f.flag(b, f.p.IsChecked), "a click never unchecks a RadioButton");
    f.ui.set(n1, f.p.IsChecked, PropertyValue(true));
    f.ui.set(n2, f.p.IsChecked, PropertyValue(true));
    check(!f.flag(n1, f.p.IsChecked) && f.flag(n2, f.p.IsChecked) && f.flag(b, f.p.IsChecked),
          "a named group spans parents and leaves other groups alone");
}

void toggleSwitch()
{
    Fixture f;
    const auto sw = f.add(ElementKind::kToggleSwitch);
    f.ui.set(sw, f.p.Header, PropertyValue(std::string("Wi-Fi")));
    f.ui.update(400, 600);
    const Box b = f.ui.bounds(sw);
    check(near(b.width, 40 + 12 + 30) && b.height > 20 + 14 * 1.33f,
          "ToggleSwitch measures header and switch");
    f.watch(sw, f.ui.events().Toggled, "toggled");
    f.watch(sw, f.ui.events().Click, "click");
    f.click(sw);
    check(f.flag(sw, f.p.IsOn) && f.log == std::vector<std::string>{"toggled:" + std::to_string(sw)},
          "a click turns it on with Toggled and no Click");
}

void toggleObjectApi()
{
    Fixture f;
    auto toggle = createElement<ToggleSwitchElement>();
    check(toggle->setIsOn(true) == PropertySetResult::kOk && toggle->isOn(),
          "typed toggle configures its actual property off-tree");
    const auto id = f.ui.insert(toggle, f.stack);
    int events = 0;
    f.ui.on(id, f.ui.events().Toggled, [&] { ++events; });
    toggle->setIsOn(false);
    toggle->setIsOn(false);
    check(!toggle->isOn() && events == 1, "typed writes raise Toggled once per effective change");
    toggle->setIsOn(true, PropertySource::kStyle);
    check(!toggle->isOn() && events == 1, "masked style writes do not raise Toggled");
    toggle->clearValue(ToggleSwitchElement::isOnProperty());
    check(toggle->isOn() && events == 2, "clearValue exposes the style and raises Toggled");
    f.ui.set(id, f.p.IsOn, false);
    check(!toggle->isOn() && events == 3, "Framework and object APIs share values and events");

    auto expression = std::make_shared<PropertyExpression>();
    toggle->setExpression(ToggleSwitchElement::isOnProperty(), PropertySource::kLocal, expression, true);
    toggle->updateExpressionValue(ToggleSwitchElement::isOnProperty(), PropertySource::kLocal, *expression,
                                  false);
    check(!toggle->isOn() && events == 5, "expression installation and updates raise toggle events");
    PropertySetResult result = PropertySetResult::kOk;
    std::thread([&] { result = toggle->setIsOn(true); }).join();
    check(result == PropertySetResult::kWrongThread && !toggle->isOn() && events == 5,
          "typed API preserves owner-thread validation");

    auto first = createElement<ToggleButtonElement>(ElementKind::kRadioButton);
    auto second = createElement<ToggleButtonElement>(ElementKind::kRadioButton);
    const auto firstId = f.ui.insert(first, f.stack);
    const auto secondId = f.ui.insert(second, f.stack);
    int unchecked = 0, checked = 0;
    f.ui.on(firstId, f.ui.events().Unchecked, [&] { ++unchecked; });
    f.ui.on(secondId, f.ui.events().Checked, [&] { ++checked; });
    first->setIsChecked(true);
    second->setIsChecked(true);
    check(!first->isChecked() && second->isChecked() && unchecked == 1 && checked == 1,
          "object writes preserve radio group exclusivity and events");

    f.ui.on(id, f.ui.events().Toggled,
            [&]
            {
                ++events;
                f.ui.tree().remove(id);
            });
    toggle->setIsOn(true);
    check(!toggle->isInTree() && toggle->isOn() && events == 6,
          "a toggle handler can remove its own element");
    toggle->setIsOn(false);
    check(!toggle->isOn() && events == 6, "detached objects no longer call the former host");
}

void toggleBindingLifetime()
{
    struct Model : Observable
    {
        bool enabled = true;
        int writes = 0;
        void setEnabled(bool value)
        {
            ++writes;
            set(enabled, value, "Enabled");
        }
    } model;
    static const std::array properties = {property(
        "Enabled", [](void* object) { return static_cast<Model*>(object)->enabled; },
        [](void* object, bool value) { static_cast<Model*>(object)->setEnabled(value); })};
    static const TypeAccessors accessors{&typeid(Model), "ToggleModel", properties, observableOf<Model>};
    registerAccessors(accessors);
    Fixture f;
    auto toggle = createElement<ToggleSwitchElement>();
    const auto id = f.ui.insert(toggle, f.stack);
    f.ui.set(id, f.p.DataContext, ObjectRef{&model, &typeid(Model)});
    int events = 0;
    f.ui.on(id, f.ui.events().Toggled, [&] { ++events; });
    f.ui.bind(id, f.p.IsOn, "Enabled", BindingMode::kTwoWay);
    check(toggle->isOn() && events == 1 && model.writes == 0,
          "initial Binding publication raises the event without writing back");
    toggle->setIsOn(false);
    check(!model.enabled && model.writes == 1 && events == 2,
          "typed TwoWay setter writes the model exactly once");
    model.setEnabled(true);
    check(toggle->isOn() && model.writes == 2 && events == 3,
          "source change does not feed back into its own setter");
    f.ui.on(id, f.ui.events().Toggled, [&] { f.ui.tree().remove(id); });
    model.setEnabled(false);
    check(!toggle->isInTree() && f.ui.bindingStats().bindings == 0,
          "source event removal releases bindings after in-flight evaluation");
    model.setEnabled(true);
    check(!toggle->isOn() && f.ui.bindingErrors().empty(), "removed target has no live source subscription");
}

void scopedEvents()
{
    Fixture f;
    const auto id = f.add(ElementKind::kCheckBox);
    const auto* event = f.ui.events().Checked;
    int calls = 0;
    const auto raise = [&] { f.ui.tree().dispatch(id, event->id); };
    {
        auto first = f.ui.subscribeEvent(id, event,
                                         [&](ElementId sender, RoutedEventArgs& args)
                                         {
                                             check(sender == id && args.originalSource == id,
                                                   "subscription receives sender and source");
                                             ++calls;
                                         });
        auto second = f.ui.subscribeEvent(id, event, [&](ElementId, RoutedEventArgs&) { calls += 10; });
        raise();
        check(calls == 11, "scoped subscriptions coexist without replacement");
        auto moved = std::move(first);
        first.reset();
        second = std::move(moved);
        raise();
        check(calls == 12, "move assignment revokes the previous subscription exactly once");
    }
    raise();
    check(calls == 12, "scope exit unregisters callbacks");

    EventSubscription later;
    auto earlier = f.ui.subscribeEvent(id, event, [&](ElementId, RoutedEventArgs&) { later.reset(); });
    later = f.ui.subscribeEvent(id, event, [&](ElementId, RoutedEventArgs&) { ++calls; });
    raise();
    check(calls == 12, "revoked callbacks in the current route snapshot are skipped");
    earlier.reset();
    EventSubscription self;
    self = f.ui.subscribeEvent(id, event,
                               [&](ElementId, RoutedEventArgs&)
                               {
                                   ++calls;
                                   self.reset();
                               });
    raise();
    raise();
    check(calls == 13, "a subscription can revoke itself during delivery");

    auto handled =
        f.ui.subscribeEvent(id, event, [](ElementId, RoutedEventArgs& args) { args.handled = true; });
    auto skipped = f.ui.subscribeEvent(id, event, [&](ElementId, RoutedEventArgs&) { calls += 100; });
    auto observed = f.ui.subscribeEvent(id, event, [&](ElementId, RoutedEventArgs&) { ++calls; }, true);
    raise();
    check(calls == 14, "scoped subscriptions respect Handled and handledEventsToo");
    handled.reset();
    skipped.reset();
    observed.reset();

    auto held = std::make_shared<int>(42);
    std::weak_ptr<int> weak = held;
    auto removed = f.ui.subscribeEvent(id, event, [held](ElementId, RoutedEventArgs&) {});
    held.reset();
    check(!weak.expired(), "event registration owns its callback captures");
    f.ui.tree().remove(id);
    check(weak.expired(), "element removal releases callback captures while the scope token survives");
    removed.reset();
    removed.reset();

    EventSubscription surviving;
    std::shared_ptr<void> retainedLifetime;
    {
        Fixture temporary;
        const auto target = temporary.add(ElementKind::kToggleSwitch);
        retainedLifetime = temporary.ui.lifetime().lock();
        surviving = temporary.ui.subscribeEvent(target, temporary.ui.events().Toggled,
                                                [](ElementId, RoutedEventArgs&) {});
    }
    surviving.reset();
    check(true, "subscription reset is safe after host destruction even with a retained lifetime token");
    auto invalid = f.ui.subscribeEvent(id, nullptr, {});
    invalid.reset();
}

void rangeObjectApi()
{
    Fixture f;
    auto slider = createElement<SliderElement>();
    check(near(slider->minimum(), 0) && near(slider->maximum(), 100) && near(slider->stepFrequency(), 1),
          "typed Slider uses schema defaults");
    slider->setMinimum(-10);
    slider->setValue(-50);
    check(near(slider->value(), -10), "off-tree RangeBase clamps typed writes");
    const auto id = f.ui.insert(slider, f.stack);
    int events = 0;
    f.ui.on(id, f.ui.events().ValueChanged, [&] { ++events; });
    slider->setValue(40);
    slider->setValue(40);
    slider->setValue(20, PropertySource::kStyle);
    check(events == 1 && near(slider->value(), 40), "range notifications use effective values");
    slider->clearValue(SliderElement::valueProperty());
    check(events == 2 && near(slider->value(), 20), "clearing local Value publishes the style");

    auto expression = std::make_shared<PropertyExpression>();
    slider->setExpression(SliderElement::valueProperty(), PropertySource::kLocal, expression, 500.0);
    check(events == 3 && near(slider->value(), 100) &&
              slider->expression(SliderElement::valueProperty(), PropertySource::kLocal) == expression.get(),
          "clamping expression values preserves the subscription");
    slider->updateExpressionValue(SliderElement::valueProperty(), PropertySource::kLocal, *expression, -30.0);
    check(events == 4 && near(slider->value(), -10), "expression updates use object range rules");
    check(slider->setValue(SliderElement::valueProperty(), PropertyValue(true)) ==
                  PropertySetResult::kInvalidValue &&
              slider->setMaximum(std::numeric_limits<f64>::infinity()) == PropertySetResult::kInvalidValue &&
              slider->setStepFrequency(-1) == PropertySetResult::kInvalidValue &&
              !slider->updateExpressionValue(SliderElement::valueProperty(), PropertySource::kLocal,
                                             *expression, std::numeric_limits<f64>::quiet_NaN()) &&
              events == 4,
          "all range writers reject invalid types and non-finite values without events");
    bool consistent = true;
    slider->addPropertyChanged(
        [&](DependencyObject&, const PropertyDescriptor&, const PropertyValue&, const PropertyValue&)
        {
            consistent =
                consistent && slider->minimum() <= slider->value() && slider->value() <= slider->maximum();
        });
    slider->setMinimum(150);
    check(consistent && near(slider->maximum(), 150) && near(slider->value(), 150) && events == 5,
          "range observers see normalized dependent properties and one ValueChanged");
    PropertySetResult result = PropertySetResult::kOk;
    std::thread([&] { result = slider->setValue(200); }).join();
    check(result == PropertySetResult::kWrongThread && events == 5, "RangeBase retains thread affinity");
    f.ui.on(id, f.ui.events().ValueChanged,
            [&]
            {
                ++events;
                f.ui.tree().remove(id);
            });
    slider->setMinimum(200);
    check(!slider->isInTree() && near(slider->value(), 200), "range callbacks may unload their control");
    slider->setMinimum(250);
    check(events == 6 && near(slider->value(), 250), "detached ranges no longer notify the former host");

    auto progress = createElement<ProgressBarElement>();
    progress->setMaximum(30);
    progress->setValue(80, PropertySource::kStyle);
    progress->setValue(15);
    progress->setMaximum(20);
    progress->clearValue(ProgressBarElement::valueProperty());
    check(near(progress->value(), 20), "clearing a range revalidates the newly exposed style value");
}

void rangeBindingLifetime()
{
    struct Model : Observable
    {
        f64 amount = 200;
        int writes = 0;
        void setAmount(f64 value)
        {
            ++writes;
            set(amount, value, "Amount");
        }
    } model;
    static const std::array properties = {property(
        "Amount", [](void* object) { return static_cast<Model*>(object)->amount; },
        [](void* object, f64 value) { static_cast<Model*>(object)->setAmount(value); })};
    static const TypeAccessors accessors{&typeid(Model), "RangeModel", properties, observableOf<Model>};
    registerAccessors(accessors);
    Fixture f;
    auto slider = createElement<SliderElement>();
    const auto id = f.ui.insert(slider, f.stack);
    f.ui.set(id, f.p.DataContext, ObjectRef{&model, &typeid(Model)});
    f.ui.bind(id, f.p.Value, "Amount", BindingMode::kTwoWay);
    check(near(slider->value(), 100) && near(model.amount, 200) && model.writes == 0,
          "Binding clamps the target without feeding a source publication back");
    slider->setValue(50);
    check(near(model.amount, 50) && model.writes == 1, "typed range writes back exactly once");
    slider->setMaximum(25);
    check(near(slider->value(), 25) && near(model.amount, 25) && model.writes == 2,
          "dependent range normalization preserves TwoWay write-back");
    f.ui.on(id, f.ui.events().ValueChanged, [&] { f.ui.tree().remove(id); });
    model.setAmount(10);
    check(!slider->isInTree() && f.ui.bindingStats().bindings == 0,
          "a range source event can unload its bound target");
    model.setAmount(5);
    check(near(slider->value(), 10) && f.ui.bindingErrors().empty(),
          "unloaded range unsubscribes from its model");
}

void slider()
{
    Fixture f;
    const auto s = f.add(ElementKind::kSlider);
    f.ui.set(s, f.p.Width, PropertyValue(220.0));
    f.ui.set(s, f.p.HorizontalAlignment, PropertyValue(std::string("Left")));
    f.ui.update(400, 600);
    int changed = 0;
    f.ui.on(s, f.ui.events().ValueChanged, [&] { ++changed; });
    const Box b = f.ui.bounds(s);
    f.ui.pointerDown(b.x + 10 + 100, b.y + 16); // the middle of the 200-DIP thumb span
    check(near(f.value(s), 50) && changed == 1 && f.ui.focused() == s, "a press moves the thumb there");
    f.ui.pointerMove(b.x + 10 + 152.6f, b.y + 40);
    check(near(f.value(s), 76), "dragging snaps to StepFrequency");
    f.ui.pointerMove(b.x + 1000, b.y);
    f.ui.pointerUp(b.x + 1000, b.y);
    check(near(f.value(s), 100), "dragging clamps at Maximum");
    f.ui.pointerMove(b.x + 10, b.y + 16);
    check(near(f.value(s), 100), "the thumb stops following after release");
    f.ui.key(Key::kLeft);
    f.ui.key(Key::kPageDown);
    check(near(f.value(s), 89), "arrow keys step, PageDown moves a tenth of the range");
    f.ui.key(Key::kHome);
    check(near(f.value(s), 0), "Home goes to Minimum");

    f.ui.set(s, f.p.Value, PropertyValue(80.0));
    changed = 0;
    f.ui.set(s, f.p.Maximum, PropertyValue(50.0));
    check(near(f.value(s), 50) && changed == 1, "lowering Maximum coerces Value and raises ValueChanged");
    f.ui.set(s, f.p.Minimum, PropertyValue(70.0));
    check(near(f.ui.tree().property(s, *f.p.Maximum).asDouble(), 70) && near(f.value(s), 70),
          "raising Minimum lifts Maximum and Value");
    f.ui.set(s, f.p.Minimum, PropertyValue(-20.0));
    f.ui.set(s, f.p.Value, PropertyValue(-50.0));
    check(near(f.value(s), -20), "negative ranges are coerced too");
}

void progressAndCanvas()
{
    Fixture f;
    const auto bar = f.add(ElementKind::kProgressBar);
    f.ui.set(bar, f.p.Value, PropertyValue(250.0));
    f.ui.update(400, 600);
    check(near(f.value(bar), 100) && near(f.ui.bounds(bar).height, 4) && near(f.ui.bounds(bar).width, 400),
          "ProgressBar coerces Value and stretches to a 4-DIP bar");

    const auto canvas = f.ui.create(ElementKind::kCanvas, f.page);
    const auto child = f.ui.create(ElementKind::kBorder, canvas);
    f.ui.tree().remove(f.stack);
    f.ui.set(child, f.p.Width, PropertyValue(30.0));
    f.ui.set(child, f.p.Height, PropertyValue(20.0));
    f.ui.set(child, f.p.CanvasLeft, PropertyValue(40.0));
    f.ui.set(child, f.p.CanvasTop, PropertyValue(25.0));
    f.ui.update(400, 600);
    const Box b = f.ui.bounds(child);
    check(near(b.x, 40) && near(b.y, 25) && near(b.width, 30) && near(b.height, 20),
          "Canvas places children at Canvas.Left/Top with their size");
    f.ui.set(child, f.p.CanvasLeft, PropertyValue(-10.0));
    f.ui.update(400, 600);
    check(near(f.ui.bounds(child).x, -10), "Canvas.Left may be negative");
}

void retainedControls()
{
    Fixture f;
    const auto boxObject = createElement(ElementKind::kCheckBox);
    const ElementId box = f.ui.insert(boxObject, f.stack);
    check(box && f.ui.tree().object(box) == boxObject, "the C++ factory object is the retained CheckBox");
    int checked = 0;
    f.ui.on(box, f.ui.events().Checked, [&] { ++checked; });
    f.ui.set(box, f.p.IsChecked, PropertyValue(true));
    check(f.flag(box, f.p.IsChecked) && checked == 1, "CheckBox property writes reach its event behavior");

    const ElementId slider = f.add(ElementKind::kSlider);
    f.ui.set(slider, f.p.Minimum, PropertyValue(-10.0));
    f.ui.set(slider, f.p.Value, PropertyValue(-50.0));
    check(near(f.value(slider), -10), "Slider accepts negative ranges and coerces Value");
    int changes = 0;
    f.ui.on(slider, f.ui.events().ValueChanged, [&] { ++changes; });
    f.ui.set(slider, f.p.Value, PropertyValue(std::numeric_limits<f64>::quiet_NaN()));
    f.ui.set(slider, f.p.Value, PropertyValue(std::numeric_limits<f64>::infinity()));
    f.ui.set(slider, f.p.Minimum, PropertyValue(-std::numeric_limits<f64>::infinity()));
    f.ui.set(slider, f.p.Maximum, PropertyValue(std::numeric_limits<f64>::quiet_NaN()));
    f.ui.set(slider, f.p.StepFrequency, PropertyValue(-1.0));
    f.ui.set(slider, f.p.StepFrequency, PropertyValue(std::numeric_limits<f64>::infinity()));
    check(near(f.value(slider), -10) && changes == 0 &&
              near(f.ui.tree().property(slider, *f.p.Minimum).asDouble(), -10) &&
              near(f.ui.tree().property(slider, *f.p.Maximum).asDouble(), 100) &&
              near(f.ui.tree().property(slider, *f.p.StepFrequency).asDouble(), 1),
          "invalid ranges and steps preserve the retained values without events");

    const ElementId radio = f.add(ElementKind::kRadioButton);
    f.ui.set(radio, f.p.GroupName, PropertyValue(std::string("A")));
    check(f.ui.tree().property(radio, *f.p.GroupName).asString() == "A", "RadioButton GroupName round-trips");
    const ElementId sw = f.add(ElementKind::kToggleSwitch);
    f.ui.set(sw, f.p.IsOn, PropertyValue(true));
    check(f.flag(sw, f.p.IsOn), "ToggleSwitch IsOn round-trips");
    const ElementId progress = f.add(ElementKind::kProgressBar);
    f.ui.set(progress, f.p.Value, PropertyValue(30.0));
    check(near(f.value(progress), 30), "ProgressBar stores its Value");
    f.ui.set(progress, f.p.Value, PropertyValue(std::numeric_limits<f64>::infinity()));
    check(near(f.value(progress), 30), "ProgressBar rejects a non-finite Value");
    const ElementId canvas = f.ui.create(ElementKind::kCanvas, f.page);
    const ElementId child = f.ui.create(ElementKind::kBorder, canvas);
    check(f.ui.tree().children(canvas) == std::vector<ElementId>{child},
          "Canvas owns its children in the retained tree");

    f.ui.tree().remove(f.page);
    check(!f.ui.tree().contains(box) && !f.ui.tree().contains(slider) && !f.ui.tree().contains(child),
          "removing a page retires its complete control subtree");
    check(!boxObject->isInTree() && f.ui.tree().element(*boxObject) == 0,
          "a held C++ element becomes detached after its page is removed");
}

} // namespace

int main()
{
    checkBox();
    radioGroups();
    toggleSwitch();
    toggleObjectApi();
    toggleBindingLifetime();
    scopedEvents();
    rangeObjectApi();
    rangeBindingLifetime();
    slider();
    progressAndCanvas();
    retainedControls();
    std::printf("controls exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
