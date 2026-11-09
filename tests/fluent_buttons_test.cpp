#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/Automation.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/ElementSchema.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/fluent/DropDownButton.hpp>
#include <velora/fluent/FluentPackage.hpp>
#include <velora/fluent/ToggleSplitButton.hpp>
#include <velora/xaml/XamlBinary.hpp>

using namespace velora;
namespace
{
int failures = 0;
void check(bool result, const char* name)
{
    std::printf("Fluent buttons %s: %s\n", name, result ? "PASS" : "FAIL");
    failures += !result;
}
void click(Framework& ui, ElementId id)
{
    const auto b = ui.bounds(id);
    ui.pointerDown(b.x + b.width / 2, b.y + b.height / 2);
    ui.pointerUp(b.x + b.width / 2, b.y + b.height / 2);
}
void properties()
{
    auto drop = createElement<DropDownButton>();
    auto split = createElement<SplitButton>();
    auto toggle = createElement<ToggleSplitButton>();
    drop->setContent("Open");
    split->setContent("Save");
    toggle->setContent("Bold");
    check(drop->content() == "Open" && split->content() == "Save" && toggle->content() == "Bold" &&
              !toggle->isChecked() && !drop->isFlyoutOpen(),
          "three actual source types have independent properties");
    check(drop->setContent(std::string("\xff")) == PropertySetResult::kInvalidValue &&
              toggle->setValue(ToggleSplitButton::isCheckedProperty(), "yes") ==
                  PropertySetResult::kInvalidValue,
          "all property writers validate content and checked kind");
    int checkedEvents = 0;
    toggle->addIsCheckedChanged([&](bool) { ++checkedEvents; });
    toggle->setIsChecked(true);
    check(checkedEvents == 0 && toggle->isChecked(),
          "off-tree initial checked value suppresses activation event");
    Compositor compositor;
    Framework ui(compositor);
    bool refused = false;
    try
    {
        ui.insert(drop);
    }
    catch (const std::runtime_error&)
    {
        refused = true;
    }
    check(refused && !drop->isInTree() && ui.tree().node_count() == 0,
          "missing family templates roll back insertion");
}

void attached()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto canvas = ui.create(ElementKind::kCanvas);
    auto drop = createElement<DropDownButton>();
    auto split = createElement<SplitButton>();
    auto toggle = createElement<ToggleSplitButton>();
    drop->setContent("Open");
    split->setContent("Save");
    toggle->setContent("Bold");
    const auto dropId = ui.insert(drop, canvas);
    const auto splitId = ui.insert(split, canvas);
    const auto toggleId = ui.insert(toggle, canvas);
    for (const auto id : {dropId, splitId, toggleId})
    {
        ui.set(id, ui.props().Width, 140.0);
        ui.set(id, ui.props().Height, 32.0);
        ui.set(id, ui.props().CanvasLeft, 20.0);
    }
    ui.set(dropId, ui.props().CanvasTop, 20.0);
    ui.set(splitId, ui.props().CanvasTop, 65.0);
    ui.set(toggleId, ui.props().CanvasTop, 110.0);
    const auto outside = ui.create(ElementKind::kButton, canvas);
    ui.set(outside, ui.props().CanvasLeft, 250.0);
    ui.set(outside, ui.props().CanvasTop, 180.0);
    ui.set(outside, ui.props().Width, 100.0);
    ui.set(outside, ui.props().Height, 32.0);
    ui.set(outside, ui.props().Content, "Outside");
    int outsideClicks = 0, picked = 0;
    ui.on(outside, ui.events().Click, [&] { ++outsideClicks; });
    std::vector<ElementId> items;
    auto flyout = std::make_shared<Flyout>(
        [&](Framework& host)
        {
            const auto panel = host.create(ElementKind::kStackPanel);
            host.set(panel, host.props().Width, 160.0);
            items.clear();
            for (const auto* text : {"First", "Second"})
            {
                const auto item = host.create(ElementKind::kButton, panel);
                host.set(item, host.props().Content, text);
                items.push_back(item);
                host.on(item, host.events().Click, [&] { ++picked; });
            }
            return panel;
        });
    drop->setFlyout(flyout);
    split->setFlyout(flyout);
    toggle->setFlyout(flyout);
    ui.update(360, 240);
    check(ui.templateChild(dropId, "PrimaryButton") && ui.templateChild(splitId, "SecondaryButton") &&
              ui.templateChild(toggleId, "SecondaryButton"),
          "all three compiled templates attach with independent namescopes");
    int dropClicks = 0, splitClicks = 0, toggleClicks = 0, checkedEvents = 0;
    auto dropClick =
        ui.subscribeEvent(dropId, ui.events().Click, [&](ElementId, RoutedEventArgs&) { ++dropClicks; });
    auto splitClick =
        ui.subscribeEvent(splitId, ui.events().Click, [&](ElementId, RoutedEventArgs&) { ++splitClicks; });
    auto toggleClick = ui.subscribeEvent(toggleId, ui.events().Click,
                                         [&](ElementId, RoutedEventArgs&)
                                         {
                                             ++toggleClicks;
                                             check(toggle->isChecked(), "toggle precedes primary Click");
                                         });
    const auto checkedToken = toggle->addIsCheckedChanged([&](bool) { ++checkedEvents; });

    click(ui, ui.templateChild(dropId, "PrimaryButton"));
    ui.update(360, 240);
    const auto firstRoot = ui.popupRoot();
    check(dropClicks == 1 && drop->isFlyoutOpen() && !split->isFlyoutOpen() && firstRoot &&
              ui.focused() == items[0],
          "DropDown click opens once, owns popup state and focuses first content item");
    const auto popup = ui.bounds(firstRoot);
    const auto anchor = ui.bounds(dropId);
    check(popup.y >= anchor.y + anchor.height && popup.x == anchor.x && popup.width < 360,
          "popup uses desired size and bottom edge-aligned placement");
    check(ui.isWithin(ui.hitTest(popup.x + 12, popup.y + 12), firstRoot),
          "popup participates in actual hit testing");
    check(ui.currentState(ui.templateChild(dropId, "RootGrid"), "FlyoutStates") == "FlyoutOpen",
          "DropDown actual template enters open visual state");
    const auto light = ui.tree().property(firstRoot, *ui.props().Background);
    ui.setTheme(Theme::kDark);
    check(ui.tree().property(firstRoot, *ui.props().Background) != light,
          "open presenter updates on theme switch");
    ui.setTheme(Theme::kHighContrast);
    check(ui.tree().property(firstRoot, *ui.props().BorderBrush).asInt() == 0xFFFFFFFF,
          "high contrast presenter has explicit visible border");
    ui.setTheme(Theme::kLight);
    ui.key(Key::kTab);
    check(ui.focused() == items[1], "Tab moves within popup");
    ui.key(Key::kTab);
    check(ui.focused() == items[0], "Tab wraps without escaping popup");
    ui.key(Key::kTab, true);
    check(ui.focused() == items[1], "Shift Tab wraps within popup");
    ui.key(Key::kSpace);
    check(picked == 1, "popup buttons use real keyboard activation");
    ui.key(Key::kEscape);
    check(!ui.popupRoot() && !flyout->isOpen() && !ui.tree().contains(firstRoot) && ui.focused() == dropId,
          "Escape removes presenter/content and restores anchor focus");
    drop->openFlyout();
    ui.update(360, 240);
    click(ui, outside);
    check(!ui.popupRoot() && outsideClicks == 0, "outside press dismisses without click-through");
    click(ui, outside);
    check(outsideClicks == 1, "subsequent outside gesture activates normally");

    click(ui, ui.templateChild(splitId, "PrimaryButton"));
    check(splitClicks == 1 && !ui.popupRoot(), "Split primary invokes without opening Flyout");
    check(ui.focused() == splitId, "mouse primary activation gives keyboard focus to composite owner");
    ui.key(Key::kF4);
    check(split->isFlyoutOpen(), "F4 works immediately after mouse primary activation");
    ui.key(Key::kEscape);
    click(ui, ui.templateChild(splitId, "SecondaryButton"));
    ui.update(360, 240);
    check(splitClicks == 1 && split->isFlyoutOpen(), "Split secondary opens without raising primary Click");
    split->closeFlyout();
    const auto uncheckedBrush =
        ui.tree().property(ui.templateChild(toggleId, "PrimaryButton"), *ui.props().Background);
    click(ui, ui.templateChild(toggleId, "PrimaryButton"));
    check(toggle->isChecked() && toggleClicks == 1 && checkedEvents == 1,
          "ToggleSplit primary toggles and invokes exactly once");
    check(ui.currentState(ui.templateChild(toggleId, "RootGrid"), "CheckStates") == "Checked",
          "ToggleSplit actual template enters checked visual state");
    check(ui.tree().property(ui.templateChild(toggleId, "PrimaryButton"), *ui.props().Background) !=
              uncheckedBrush,
          "checked visual state changes actual button brush");
    click(ui, ui.templateChild(toggleId, "SecondaryButton"));
    ui.update(360, 240);
    check(toggle->isChecked() && toggleClicks == 1 && checkedEvents == 1 && toggle->isFlyoutOpen(),
          "ToggleSplit secondary preserves checked value and primary event count");
    ui.key(Key::kEscape);
    ui.focus(splitId);
    ui.key(Key::kF4);
    ui.update(360, 240);
    check(split->isFlyoutOpen(), "F4 opens shared Flyout path");
    ui.key(Key::kEscape);
    ui.focus(dropId);
    ui.key(Key::kDown, false, false, true);
    ui.update(360, 240);
    check(drop->isFlyoutOpen(), "Alt Down opens shared Flyout path");
    ui.set(canvas, ui.props().IsEnabled, false);
    check(!ui.popupRoot() && !drop->openFlyout(), "disabling anchor ancestor closes and blocks popup");
    ui.set(canvas, ui.props().IsEnabled, true);
    ui.focus(splitId);
    ui.key(Key::kSpace);
    check(splitClicks == 2, "source button Space invokes through root keyboard route");

    ui.set(dropId, ui.props().CanvasLeft, 300.0);
    ui.set(dropId, ui.props().CanvasTop, 210.0);
    ui.update(360, 240);
    drop->openFlyout();
    ui.update(360, 240);
    auto b = ui.bounds(ui.popupRoot());
    check(b.x >= 0 && b.x + b.width <= 360 && b.y < 210 && b.y + b.height <= 240,
          "edge popup flips above and clamps horizontally");
    ui.update(220, 120);
    b = ui.bounds(ui.popupRoot());
    check(b.x >= 0 && b.y >= 0 && b.x + b.width <= 220 && b.y + b.height <= 120,
          "resize repositions popup inside viewport");
    ui.setMounted(canvas, false);
    check(!ui.popupRoot() && !drop->openFlyout(), "unmount closes popup and rejects detached visual anchor");
    ui.setMounted(canvas, true);
    ui.set(dropId, ui.props().CanvasLeft, 20.0);
    ui.set(dropId, ui.props().CanvasTop, 20.0);
    ui.update(360, 240);
    drop->openFlyout();
    ui.update(360, 240);
    const auto removedPopup = ui.popupRoot();
    ui.tree().remove(removedPopup);
    check(!drop->isFlyoutOpen() && !ui.popupRoot(), "external presenter removal settles Flyout state");
    const auto baseline = ui.tree().node_count();
    for (int i = 0; i < 30; ++i)
    {
        drop->openFlyout();
        drop->closeFlyout();
    }
    check(ui.tree().node_count() == baseline, "repeated opens leave only the original control tree");
    drop->openFlyout();
    ui.tree().remove(dropId);
    check(!ui.popupRoot() && !flyout->isOpen() && !drop->isInTree(),
          "anchor removal tears down independent popup root");
    const auto reattached = ui.insert(drop, canvas);
    ui.update(360, 240);
    check(ui.templateChild(reattached, "PrimaryButton") && drop->openFlyout(),
          "reattachment rebuilds template and working subscriptions");
    drop->closeFlyout();
    toggle->removePropertyChanged(checkedToken);
    ui.bindElement(toggleId, &ToggleSplitButton::isCheckedProperty(), reattached, ui.props().IsEnabled,
                   BindingMode::kTwoWay);
    toggle->setIsChecked(false);
    check(!ui.tree().property(reattached, *ui.props().IsEnabled).asBool(),
          "ToggleSplit checked TwoWay writes the actual source property");

    check(DropDownButton::registerType(ui).empty() && SplitButton::registerType(ui).empty() &&
              ToggleSplitButton::registerType(ui).empty(),
          "all family factories register together");
    for (const auto* declaration :
         {&DropDownButton::declaration(), &SplitButton::declaration(), &ToggleSplitButton::declaration()})
    {
        XamlIR ir;
        ir.sourceName = "family.xaml";
        ir.schemaFingerprint = schemaFingerprint();
        const auto type = ir.type(declaration->className);
        ir.types[type].contract = declaration->contract();
        ir.instructions = {{XamlIROp::kBeginObject, type, 0, 0, 1, 1}, {XamlIROp::kEndObject, 0, 0, 0, 1, 2}};
        const auto loaded = loadComponent(ui, writeVxbf(ir));
        check(loaded.ok && loaded.root->className() == declaration->className,
              "compiled XAML creates actual family source class");
    }
}

void lifetime()
{
    auto flyout = std::make_shared<Flyout>([](Framework& ui) { return ui.create(ElementKind::kButton); });
    {
        Compositor compositor;
        Framework ui(compositor);
        auto drop = createElement<DropDownButton>();
        drop->setFlyout(flyout);
        const auto id = ui.insert(drop);
        ui.update(300, 200);
        check(drop->openFlyout(), "Flyout opens before UI context teardown");
        (void)id;
    }
    check(!flyout->isOpen(), "Flyout may outlive UI context safely");
    Compositor compositor;
    Framework ui(compositor);
    auto toggle = createElement<ToggleSplitButton>();
    std::weak_ptr<ToggleSplitButton> weak = toggle;
    const auto id = ui.insert(toggle);
    ui.update(300, 200);
    toggle->addIsCheckedChanged(
        [&](bool)
        {
            ui.tree().remove(id);
            toggle.reset();
        });
    ui.focus(id);
    ui.key(Key::kSpace);
    check(weak.expired() && !ui.tree().node_count(),
          "toggle notification may unload and release last external reference");
    auto anchor = ui.create(ElementKind::kButton);
    ui.update(300, 200);
    auto bad = std::make_shared<Flyout>([](Framework&) -> ElementId
                                        { throw std::runtime_error("factory rejected"); });
    const auto before = ui.tree().node_count();
    check(!bad->showAt(ui, anchor) && bad->lastError() == "factory rejected" &&
              ui.tree().node_count() == before,
          "factory failure removes compiled presenter and retains diagnostic");
    auto own = std::make_shared<Flyout>([](Framework& host) { return host.create(ElementKind::kButton); });
    check(own->showAt(ui, anchor), "independent Flyout opens");
    own.reset();
    check(!ui.popupRoot() && ui.tree().node_count() == before, "Flyout destruction closes owned presenter");
    auto reentrant =
        std::make_shared<Flyout>([](Framework& host) { return host.create(ElementKind::kButton); });
    int reopened = 0;
    reentrant->showAt(ui, anchor,
                      [&](bool open)
                      {
                          if (!open && reopened++ == 0)
                              reentrant->showAt(ui, anchor);
                      });
    reentrant->hide();
    check(reentrant->isOpen() && ui.popupRoot(),
          "close callback can reopen without old hide erasing new state");
    reentrant->hide();
    Compositor otherCompositor;
    Framework other(otherCompositor);
    auto otherDrop = createElement<DropDownButton>();
    const auto otherId = other.insert(otherDrop);
    other.update(300, 200);
    Compositor originalCompositor;
    Framework original(originalCompositor);
    auto originalDrop = createElement<DropDownButton>();
    const auto originalId = original.insert(originalDrop);
    original.update(300, 200);
    auto shared = std::make_shared<Flyout>([](Framework& host) { return host.create(ElementKind::kButton); });
    originalDrop->setFlyout(shared);
    otherDrop->setFlyout(shared);
    originalDrop->openFlyout();
    check(originalId == otherId && originalDrop->isFlyoutOpen() && !otherDrop->isFlyoutOpen(),
          "Flyout identity includes UI context, not just element ID");
    otherDrop->openFlyout();
    check(!originalDrop->isFlyoutOpen() && otherDrop->isFlyoutOpen() && !original.popupRoot(),
          "shared Flyout moves across UI contexts without retaining old presenter");
    otherDrop->closeFlyout();
}
} // namespace
int main(int argc, char** argv)
{
    properties();
    if (argc == 2)
    {
        if (const auto error = mountFluent(argv[1]); !error.empty())
        {
            std::puts(error.c_str());
            return 1;
        }
        attached();
        lifetime();
    }
    else
        std::puts("Fluent button template checks: NOT_RUN (no compiled package supplied)");
    return failures ? 1 : 0;
}
