// GalleryPage.xaml through the .vxbf pipeline on a real Win32 window with Direct2D/DirectWrite.
// Core controls are driven through pointer, keyboard and wheel input; the controller state,
// generated bindings, retained properties and read-back pixels are checked.
// Screenshots go to argv[1] if given.
#include "GalleryPage.g.hpp"
#include <velora/xaml/XamlLoader.hpp>

#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/BuiltinElements.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/platform/windowing/NativeWindow.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/text/TextService.hpp>
#include <windows.h>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("gallery %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

struct Pixel
{
    int r = 0, g = 0, b = 0;
};

constexpr float kWidth = 960, kHeight = 720;

class Harness
{
  public:
    Harness(GraphicsDevice& device, TextService& text, NativeWindow& window, std::string out)
        : m_out(std::move(out)),
          ui(compositor, [&text](std::string_view s, float size) { return text.measureWidth(s, size); })
    {
        ready = surface.attach(device, window.nativeHandle(),
                               {{static_cast<u32>(kWidth), static_cast<u32>(kHeight)}, 1.0f, false});
        Gallery::GalleryPage_component::Names componentNames;
        const auto loaded =
            Gallery::GalleryPage_component::initializeComponent(ui, controller, componentNames);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        root = ui.tree().element(*loaded.root);
#define NAME(member) names.member = decltype(names.member){ui.tree().element(*componentNames.member)}
        NAME(Scroller);
        NAME(NameBox);
        NAME(GreetButton);
        NAME(GreetingText);
        NAME(Subscribe);
        NAME(Newsletter);
        NAME(Notify);
        NAME(Small);
        NAME(Medium);
        NAME(Large);
        NAME(VolumeSlider);
        NAME(Progress);
        NAME(Shapes);
        NAME(FruitList);
        NAME(SizeText);
        NAME(SummaryText);
#undef NAME
        frame();
    }

    ~Harness()
    {
        // The controller is a member declared after ui: remove its bindings before it dies.
        if (root)
            ui.tree().remove(root);
    }

    bool frame(const char* screenshot = nullptr)
    {
        ui.update(kWidth, kHeight);
        compositor.resize(kWidth, kHeight, 1);
        const auto scene = compositor.snapshot();
        const bool drawn = scene.displayList && surface.draw(*scene.displayList, &capture);
        if (drawn && screenshot && !m_out.empty())
            (void)saveCapturePng(capture, m_out + "/" + screenshot);
        return drawn;
    }
    Pixel at(float x, float y) const
    {
        const auto px = static_cast<u32>(x), py = static_cast<u32>(y);
        if (px >= capture.width || py >= capture.height)
            return {-1, -1, -1};
        const u8* p = capture.bgra.data() + (std::size_t{py} * capture.width + px) * 4;
        return {p[2], p[1], p[0]};
    }
    // Clicks `dx` DIPs from the element's left edge, vertically centred.
    void click(ElementId e, float dx = 8)
    {
        reveal(e);
        const Box b = ui.bounds(e);
        ui.pointerDown(b.x + dx, b.y + b.height / 2);
        ui.pointerUp(b.x + dx, b.y + b.height / 2);
        frame();
    }
    // Scrolls the gallery until the element is fully inside the viewport.
    void reveal(ElementId e)
    {
        const Box v = ui.bounds(names.Scroller), b = ui.bounds(e);
        const double offset = ui.tree().property(names.Scroller, *ui.props().VerticalOffset).asDouble();
        if (b.y < v.y || b.y + b.height > v.y + v.height)
            ui.changeView(names.Scroller, std::nullopt, offset + (b.y - v.y) - 40);
        frame();
    }
    std::string text(ElementId e) const
    {
        return std::string(ui.tree().property(e, *ui.props().Text).asString());
    }

    bool ready = false;
    Compositor compositor;
    GraphicsSurface surface;
    FrameCapture capture;
    std::string m_out;
    Framework ui;
    Gallery::GalleryPage controller;
    ElementId root = 0;
    struct Names
    {
        TypedElementId<ElementKind::kScrollViewer> Scroller;
        TypedElementId<ElementKind::kTextBox> NameBox;
        TypedElementId<ElementKind::kButton> GreetButton;
        TypedElementId<ElementKind::kTextBlock> GreetingText;
        TypedElementId<ElementKind::kCheckBox> Subscribe;
        TypedElementId<ElementKind::kCheckBox> Newsletter;
        TypedElementId<ElementKind::kToggleSwitch> Notify;
        TypedElementId<ElementKind::kRadioButton> Small;
        TypedElementId<ElementKind::kRadioButton> Medium;
        TypedElementId<ElementKind::kRadioButton> Large;
        TypedElementId<ElementKind::kSlider> VolumeSlider;
        TypedElementId<ElementKind::kProgressBar> Progress;
        TypedElementId<ElementKind::kCanvas> Shapes;
        TypedElementId<ElementKind::kListView> FruitList;
        TypedElementId<ElementKind::kTextBlock> SizeText;
        TypedElementId<ElementKind::kTextBlock> SummaryText;
    } names;
};

bool accent(Pixel p)
{
    return p.r < 40 && p.g > 90 && p.g < 150 && p.b > 180; // #0078D4
}

void run(Harness& h)
{
    auto& ui = h.ui;
    const auto& n = h.names;
    check(h.ready && h.frame("gallery-top.png"), "gallery page drawn on the D2D surface");

    // Initial values from XAML literals and OneWay/TwoWay x:Bind.
    check(ui.tree().is(n.Notify, ElementKind::kToggleSwitch) &&
              ui.tree().property(n.Notify, *ui.props().IsOn).asBool(),
          "ToggleSwitch starts on from its TwoWay source");
    h.reveal(n.Small);
    const Box smallBox = ui.bounds(n.Small);
    check(ui.tree().property(n.Small, *ui.props().IsChecked).asBool() &&
              accent(h.at(smallBox.x + 3, smallBox.y + smallBox.height / 2)),
          "the checked RadioButton is drawn in the accent colour");
    check(ui.tree().property(n.VolumeSlider, *ui.props().Value).asDouble() == 40 &&
              ui.tree().property(n.Progress, *ui.props().Value).asDouble() == 40,
          "Slider and ProgressBar start at the bound volume");

    // Text input and Button.
    h.click(n.NameBox);
    ui.textInput("Ada");
    h.click(n.GreetButton);
    check(h.controller.name() == "Ada" && h.controller.greeting() == "Hello, Ada!" &&
              h.text(n.GreetingText) == "Hello, Ada!",
          "typing, TwoWay Text and a Click handler");

    // Toggles: x:Bind TwoWay CheckBox, {Binding} TwoWay CheckBox, ToggleSwitch.
    h.click(n.Subscribe);
    const Box box = ui.bounds(n.Subscribe);
    check(h.controller.subscribed() && accent(h.at(box.x + 3, box.y + box.height / 2)),
          "a CheckBox click writes its x:Bind source and draws checked");
    h.click(n.Newsletter);
    check(h.controller.settings().newsletter() && ui.bindingErrors().empty(),
          "a CheckBox writes its {Binding} TwoWay source");
    h.controller.settings().setNewsletter(false);
    h.frame();
    check(!ui.tree().property(n.Newsletter, *ui.props().IsChecked).asBool(),
          "{Binding} follows the source back");
    h.click(n.Notify, 20);
    check(!h.controller.notifications(), "a ToggleSwitch click writes its source");

    // Choices: RadioButton group and Checked handlers.
    h.click(n.Large);
    check(h.controller.size() == "Large" && !ui.tree().property(n.Small, *ui.props().IsChecked).asBool() &&
              h.text(n.SizeText) == "Large",
          "a RadioButton click checks it, unchecks its sibling and runs its handler");

    // Range: pointer, keyboard and source changes.
    h.reveal(n.VolumeSlider);
    const Box slider = ui.bounds(n.VolumeSlider);
    ui.pointerDown(slider.x + 10 + (slider.width - 20) * 0.81f, slider.y + slider.height / 2);
    ui.pointerUp(slider.x + 10 + (slider.width - 20) * 0.81f, slider.y + slider.height / 2);
    h.frame();
    check(h.controller.volume() == 80 && ui.tree().property(n.Progress, *ui.props().Value).asDouble() == 80,
          "a Slider press snaps to StepFrequency and drives the ProgressBar through the source");
    ui.key(Key::kRight);
    check(h.controller.volume() == 85, "the focused Slider steps with the arrow keys");
    h.controller.setVolume(25);
    h.frame();
    check(ui.tree().is(n.VolumeSlider, ElementKind::kSlider) &&
              ui.tree().property(n.VolumeSlider, *ui.props().Value).asDouble() == 25,
          "a source change reaches the retained Slider property");
    check(h.text(n.SummaryText).ends_with(", 25%"), "the summary follows every source");

    // Canvas and ListView further down: the wheel scrolls the page.
    const int changes = h.controller.viewChanges;
    const Box scroller = ui.bounds(n.Scroller);
    for (int i = 0; i < 20; ++i)
        ui.wheel(scroller.x + 20, scroller.y + 20, 144);
    h.frame("gallery-bottom.png");
    check(h.controller.viewChanges > changes, "wheel scrolling raises ViewChanged");
    const Box list = ui.bounds(n.FruitList), shapes = ui.bounds(n.Shapes);
    check(list.y + list.height <= scroller.y + scroller.height + 1 && list.y > scroller.y,
          "the page scrolls to its last section");
    // The first Canvas child is a red circle at Canvas.Left 0 / Top 10, 60 DIPs wide.
    const Pixel red = h.at(shapes.x + 30, shapes.y + 40);
    check(red.r > 200 && red.g < 60 && red.b < 80, "Canvas children are drawn at their positions");
    check(ui.viewport(n.FruitList).count == 8, "the ListView shows the bound fruits");

    // Ordinary C++ setters use the same objects created by the compiled XAML loader.
    auto* volume = dynamic_cast<SliderElement*>(ui.tree().uiElement(n.VolumeSlider));
    check(volume != nullptr, "compiled XAML creates the typed Slider object");
    if (volume)
    {
        volume->setValue(35);
        check(h.controller.volume() == 35 &&
                  ui.tree().property(n.Progress, *ui.props().Value).asDouble() == 35,
              "typed range Value writes back through x:Bind and updates ProgressBar");
    }
    auto* notification = dynamic_cast<ToggleSwitchElement*>(ui.tree().uiElement(n.Notify));
    auto* newsletter = dynamic_cast<ToggleButtonElement*>(ui.tree().uiElement(n.Newsletter));
    check(notification && newsletter, "compiled XAML creates the typed Toggle objects");
    if (!notification || !newsletter)
        return;
    notification->setIsOn(true);
    check(h.controller.notifications(), "typed IsOn writes back through x:Bind");
    newsletter->setIsChecked(true);
    check(h.controller.settings().newsletter(), "typed IsChecked writes back through Binding");
    int unchecked = 0;
    ui.on(n.Newsletter, ui.events().Unchecked, [&] { ++unchecked; });
    h.controller.settings().setNewsletter(false);
    check(!newsletter->isChecked() && unchecked == 1 && ui.bindingErrors().empty(),
          "source-driven Binding raises Unchecked once without write-back recursion");
    // Removal during source publication must not destroy an in-flight binding evaluation.
    ui.on(n.Newsletter, ui.events().Checked, [&] { ui.tree().remove(n.Newsletter); });
    h.controller.settings().setNewsletter(true);
    check(!ui.tree().contains(n.Newsletter), "a source-driven control event may unload its target");
    h.controller.settings().setNewsletter(false);
    check(ui.bindingErrors().empty(), "removed toggle no longer observes the binding source");
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string out;
    if (argc > 1)
    {
        out = argv[1];
        std::filesystem::create_directories(out);
    }
    GraphicsDevice device;
    TextService text;
    check(device.initialize() && text.initialize(), "Direct2D and DirectWrite initialized");
    NativeWindow window;
    WindowDesc desc;
    desc.title = "Velora gallery test";
    desc.width = static_cast<int>(kWidth);
    desc.height = static_cast<int>(kHeight);
    check(window.create(desc), "window created");
    window.pump();
    {
        Harness harness(device, text, window, out);
        run(harness);
    }
    window.close();
    std::printf("gallery exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
