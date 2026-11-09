#include <cmath>
#include <cstdio>
#include <limits>
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
#include <velora/fluent/FluentPackage.hpp>
#include <velora/fluent/InitialsGenerator.hpp>
#include <velora/fluent/NumberBox.hpp>
#include <velora/fluent/NumberBoxParser.hpp>
#include <velora/fluent/PersonPicture.hpp>
#include <velora/graphics/DisplayList.hpp>
#include <velora/xaml/XamlBinary.hpp>

using namespace velora;
namespace
{
int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("Fluent %s: %s\n", name, passed ? "PASS" : "FAIL");
    failures += !passed;
}

void initials()
{
    for (const auto& [name, expected] :
         std::vector<std::pair<std::string, std::string>>{{"", ""},
                                                          {"A", "A"},
                                                          {"Ada", "A"},
                                                          {"ada lovelace", "AL"},
                                                          {"  John  Smith  ", "JS"},
                                                          {"John Smith (team)", "JS"},
                                                          {"John Smith [team]", "JS"},
                                                          {"John Smith {team}", "JS"},
                                                          {"...Alice !Baker", "AB"},
                                                          {"!!!", "!"},
                                                          {"张三", ""},
                                                          {"العربية", ""},
                                                          {"Αλφα", ""},
                                                          {"a\xcc\x81lice baker", "A\xcc\x81"
                                                                                  "B"},
                                                          {"\xF0\xA0\x80\x80", ""},
                                                          {"   ", ""}})
        check(InitialsGenerator::fromDisplayName(name) == expected, ("initials: " + name).c_str());
}

void properties()
{
    auto picture = createElement<PersonPicture>();
    check(picture->actualInitials().empty() && picture->visualState() == "NoPhotoOrInitials" &&
              picture->badgeState() == "NoBadge",
          "off-tree defaults");
    picture->setDisplayName("Ada Lovelace");
    check(picture->actualInitials() == "AL" && picture->visualState() == "Initials", "display name initials");
    picture->setInitials("XY");
    picture->setProfilePicture("portrait.png");
    picture->setIsGroup(true);
    check(picture->visualState() == "Group", "group outranks photo and initials");
    picture->setIsGroup(false);
    check(picture->visualState() == "Photo" && picture->actualInitials() == "XY",
          "group restores personal state");
    picture->setProfilePicture("");
    check(picture->actualInitials() == "XY", "explicit initials outrank display name");
    picture->clearValue(PersonPicture::initialsProperty());
    check(picture->actualInitials() == "AL", "clearing initials restores display-derived initials");

    picture->setBadgeGlyph("!");
    picture->setBadgeNumber(-1);
    check(picture->badgeState() == "NoBadge", "negative nonzero badge number suppresses glyph, as in 2.3");
    picture->setBadgeNumber(0);
    check(picture->badgeLabel() == "!", "zero numeric badge falls back to glyph");
    picture->setBadgeNumber(100);
    check(picture->badgeLabel() == "99+", "badge number caps display at 99+");
    picture->setBadgeImageSource("badge.png");
    check(picture->badgeState() == "BadgeWithImageSource" && picture->badgeLabel().empty(),
          "image badge has priority");
    picture->setBadgeText("messages");
    check(picture->automationName() == "Ada Lovelace, 100 messages",
          "automation name uses actual count and BadgeText");
    check(picture->setValue(PersonPicture::badgeNumberProperty(), PropertyValue(true)) ==
                  PropertySetResult::kInvalidValue &&
              picture->setValue(PersonPicture::badgeNumberProperty(), i64{std::numeric_limits<i64>::max()}) ==
                  PropertySetResult::kInvalidValue &&
              picture->setDisplayName(std::string("\xff")) == PropertySetResult::kInvalidValue,
          "all property writers reject invalid types, Int32 overflow and malformed UTF-8");
    Compositor compositor;
    Framework ui(compositor);
    bool refused = false;
    try
    {
        ui.insert(picture);
    }
    catch (const std::runtime_error&)
    {
        refused = true;
    }
    check(refused && !picture->isInTree() && ui.tree().node_count() == 0,
          "missing compiled template refuses attachment and rolls back the insertion");
}

void collect(const std::vector<Command>& in, std::vector<Command>& out)
{
    for (const auto& c : in)
        if (const auto* layer = std::get_if<LayerRef>(&c))
            collect(layer->commands->commands, out);
        else
            out.push_back(c);
}

void attached()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto panel = ui.create(ElementKind::kStackPanel);
    auto first = createElement<PersonPicture>();
    auto second = createElement<PersonPicture>();
    first->setDisplayName("Ada Lovelace");
    second->setDisplayName("Grace Hopper");
    const auto a = ui.insert(first, panel), b = ui.insert(second, panel);
    first->setBadgeNumber(7);
    ui.update(400, 300);
    const auto rootA = ui.tree().children(a).front(), rootB = ui.tree().children(b).front();
    check(ui.currentState(rootA, "CommonStates") == "Initials" &&
              ui.currentState(rootA, "BadgeStates") == "BadgeWithoutImageSource",
          "compiled template states follow actual properties");
    check(ui.templateChild(a, "InitialsTextBlock") != ui.templateChild(b, "InitialsTextBlock"),
          "each control owns an independent template namescope");
    check(ui.tree().property(ui.templateChild(a, "InitialsTextBlock"), *ui.props().Text).asString() == "AL" &&
              ui.tree().property(ui.templateChild(b, "InitialsTextBlock"), *ui.props().Text).asString() ==
                  "GH",
          "template parts are actual UI elements");
    check(std::abs(
              ui.tree().property(ui.templateChild(a, "InitialsTextBlock"), *ui.props().FontSize).asDouble() -
              42) < .01,
          "2.3 initials font uses 42 percent of the diameter");
    first->setIsGroup(true);
    check(ui.currentState(rootA, "CommonStates") == "Group" &&
              ui.currentState(rootB, "CommonStates") == "Initials",
          "group changes are local to the control");
    first->setIsGroup(false);
    first->setProfilePicture("portrait.png");
    ui.update(400, 300);
    std::vector<Command> commands;
    collect(compositor.snapshot().displayList->commands(), commands);
    bool crop = false, circularClip = false;
    for (const auto& command : commands)
    {
        if (const auto* image = std::get_if<ImageCommand>(&command))
            crop |= image->path == "portrait.png" && image->fit == ImageFit::kCover;
        if (const auto* clip = std::get_if<ClipRoundedRectCommand>(&command))
            circularClip |= clip->radius == 50 && clip->bounds.width == 100;
    }
    check(crop && circularClip, "photo uses fill crop and a circular visual clip");
    const auto tree = automationTree(ui, a);
    check(tree.type == AutomationControlType::kText && tree.name == "Ada Lovelace, 7 items" &&
              tree.children.empty(),
          "automation uses the control identity and hides template parts");
    const auto label = ui.templateChild(b, "InitialsTextBlock");
    const auto light = ui.tree().property(label, *ui.props().Foreground);
    ui.setTheme(Theme::kDark);
    check(ui.tree().property(label, *ui.props().Foreground) != light,
          "Fluent theme resources update existing controls");
    ui.setTheme(Theme::kHighContrast);
    check(ui.tree()
                  .property(ui.templateChild(b, "AvatarCircle"), *ui.props().BorderThickness)
                  .asThickness()
                  .left == 1,
          "high contrast supplies an explicit avatar border");
    ui.tree().remove(panel);
    check(!first->isInTree() && !second->isInTree(), "unload detaches both controls");
    first->setDisplayName("Alan Turing");
    const auto again = ui.insert(first);
    ui.update(150, 150);
    check(again &&
              ui.tree().property(ui.templateChild(again, "InitialsTextBlock"), *ui.props().Text).asString() ==
                  "AT",
          "a detached control can create a fresh template when reattached");
    ui.tree().remove(again);

    check(PersonPicture::registerType(ui).empty(),
          "Fluent control factory registers its internal declaration");
    XamlIR ir;
    ir.sourceName = "person.xaml";
    ir.schemaFingerprint = schemaFingerprint();
    const auto type = ir.type(first->className());
    ir.types[type].contract = PersonPicture::declaration().contract();
    const auto prop =
        ir.property(PersonPicture::displayNameProperty().id, PersonPicture::displayNameProperty().name);
    const auto value = ir.value(PropertyValue("Ada Lovelace"));
    ir.instructions = {{XamlIROp::kBeginObject, type, 0, 0, 1, 1},
                       {XamlIROp::kSetValue, prop, value, 0, 1, 4},
                       {XamlIROp::kEndObject, 0, 0, 0, 1, 20}};
    const auto loaded = loadComponent(ui, writeVxbf(ir));
    check(loaded.ok && std::dynamic_pointer_cast<PersonPicture>(loaded.root),
          "compiled type records create the internal Fluent class");

    Compositor retiringCompositor;
    Framework retiringUi(retiringCompositor);
    auto retiring = createElement<PersonPicture>();
    const std::weak_ptr<PersonPicture> weak = retiring;
    const auto retiringId = retiringUi.insert(retiring);
    const auto labelObject =
        retiringUi.tree().object(retiringUi.templateChild(retiringId, "InitialsTextBlock"));
    labelObject->addPropertyChanged(
        [&](DependencyObject&, const PropertyDescriptor& property, const PropertyValue&, const PropertyValue&)
        {
            if (property.name == "Text")
            {
                retiringUi.tree().remove(retiringId);
                retiring.reset();
            }
        });
    retiring->setDisplayName("Alan Turing");
    check(weak.expired() && retiringUi.tree().node_count() == 0,
          "template-part callbacks may unload the control and release its last external reference");
}
void numberProperties()
{
    for (const auto& [input, expected] : std::vector<std::pair<std::string, f64>>{
             {"2+3*4", 14}, {"(2+3)*4", 20}, {"2^3^2", 64}, {"-2*-3", 6}, {"1e-3 + 1", 1.001}})
        check(NumberBoxParser::compute(input) == expected, ("NumberBox expression: " + input).c_str());
    for (const auto* input :
         {"", "()", "1+", "(1", "1)", "1 2", "2(3)", "1/0", "2^9999", "nan", "inf", "-(2)", "1;2"})
        check(!NumberBoxParser::compute(input),
              (std::string("NumberBox rejects expression: ") + input).c_str());
    const std::string bounded = "2+3garbage";
    check(NumberBoxParser::compute(std::string_view(bounded.data(), 3)) == 5 &&
              !NumberBoxParser::compute("2+3", false),
          "NumberBox bounded input and expression opt-in");
    auto box = createElement<NumberBox>();
    check(box->value() == 0 && box->text() == "0" && box->smallChange() == 1 && box->largeChange() == 10,
          "NumberBox upstream numeric defaults");
    box->setMaximum(10);
    box->setMinimum(2);
    int events = 0;
    f64 observedBefore = 0, observedAfter = 0;
    const auto token = box->addValueChanged(
        [&](f64 before, f64 after)
        {
            ++events;
            observedBefore = before;
            observedAfter = after;
        });
    box->setValue(50);
    check(box->value() == 10 && box->text() == "10" && events == 1 && observedBefore == 2 &&
              observedAfter == 10,
          "NumberBox normalization publishes one final ValueChanged");
    box->setValue(50);
    check(events == 1, "NumberBox clipped writes do not publish spurious value changes");
    box->setAcceptsExpression(true);
    box->setText("1+2*2");
    check(box->value() == 5 && box->text() == "5", "NumberBox Text evaluates and formats Value");
    box->setText("broken");
    check(box->value() == 5 && box->text() == "5", "NumberBox invalid text restores formatted value");
    box->setValidationMode("Disabled");
    box->setText("broken");
    check(box->text() == "broken" && box->value() == 5, "NumberBox disabled validation retains invalid text");
    box->setValue(20);
    check(box->value() == 20, "NumberBox disabled validation permits out-of-range values");
    box->setValidationMode("InvalidInputOverwritten");
    check(box->value() == 10 && box->text() == "10", "NumberBox validation mode restores range");
    box->setIsWrapEnabled(true);
    box->stepValue(1);
    check(box->value() == 2, "NumberBox wraps past maximum");
    box->stepValue(-1);
    check(box->value() == 10, "NumberBox wraps below minimum");
    box->setMaximum(1);
    check(box->minimum() == 1 && box->maximum() == 1 && box->value() == 1,
          "NumberBox Maximum lowers Minimum consistently");
    box->setMinimum(3);
    check(box->minimum() == 3 && box->maximum() == 3 && box->value() == 3,
          "NumberBox Minimum raises Maximum consistently");
    box->setText("  ");
    const int emptyEvents = events;
    box->setText("");
    box->stepValue(1);
    check(std::isnan(box->value()) && box->text().empty() && events == emptyEvents,
          "NumberBox empty text is NaN and spin preserves empty value");
    check(box->setSmallChange(std::numeric_limits<f64>::infinity()) == PropertySetResult::kInvalidValue &&
              box->setValue(NumberBox::valueProperty(), true) == PropertySetResult::kInvalidValue &&
              box->setSpinButtonPlacementMode("Compact") == PropertySetResult::kInvalidValue &&
              box->setValidationMode("unknown") == PropertySetResult::kInvalidValue &&
              box->setHeader(std::string("\xff")) == PropertySetResult::kInvalidValue,
          "NumberBox all writers reject invalid values and unsupported modes");
    box->removePropertyChanged(token);
    box->setValidationMode("Disabled");
    box->setValue(1.23456789012345);
    box->setText("invalid");
    box->cancelInput();
    check(box->value() == 1.23456789012345 && box->text() != "invalid",
          "NumberBox cancel restores formatted text without rounding stored Value");
    auto styled = createElement<NumberBox>();
    styled->setValue(NumberBox::maximumProperty(), 10.0, PropertySource::kStyle);
    styled->setValue(NumberBox::valueProperty(), 50.0, PropertySource::kStyle);
    check(styled->value() == 10 && styled->text() == "10",
          "NumberBox style writes use the same validation and normalization");
    Compositor compositor;
    Framework ui(compositor);
    bool refused = false;
    try
    {
        ui.insert(styled);
    }
    catch (const std::runtime_error&)
    {
        refused = true;
    }
    check(refused && !styled->isInTree() && ui.tree().node_count() == 0,
          "NumberBox missing template rolls back attachment");
}

void numberAttached()
{
    Compositor compositor;
    Framework ui(compositor);
    auto box = createElement<NumberBox>();
    box->setHeader("Quantity");
    box->setMinimum(0);
    box->setMaximum(100);
    box->setAcceptsExpression(true);
    box->setSpinButtonPlacementMode("Inline");
    const auto id = ui.insert(box);
    ui.update(320, 120);
    const auto input = ui.templateChild(id, "InputBox");
    const auto up = ui.templateChild(id, "UpSpinButton");
    check(input && up && ui.bounds(input).width > 0,
          "NumberBox actual compiled template attaches and lays out");
    ui.focus(input);
    ui.key(Key::kA, false, true);
    ui.textInput("2+3*4");
    check(box->value() == 0, "NumberBox draft stays uncommitted while editing");
    ui.key(Key::kEnter);
    check(box->value() == 14 && box->text() == "14" &&
              ui.tree().property(input, *ui.props().Text).asString() == "14",
          "NumberBox Enter commits expression through actual input route");
    ui.key(Key::kUp);
    ui.key(Key::kPageUp);
    ui.key(Key::kDown);
    check(box->value() == 24, "NumberBox arrow and page keys step the actual object");
    ui.key(Key::kA, false, true);
    ui.textInput("invalid");
    ui.key(Key::kEscape);
    check(box->value() == 24 && ui.tree().property(input, *ui.props().Text).asString() == "24",
          "NumberBox Escape discards uncommitted draft");
    ui.key(Key::kA, false, true);
    ui.textInput("8");
    ui.focus(up);
    ui.key(Key::kEnter);
    check(box->value() == 9, "NumberBox focus loss validates before spin-button activation");
    box->setValue(100);
    check(!ui.tree().property(up, *ui.props().IsEnabled).asBool(),
          "NumberBox disables increment at upper bound");
    ui.set(id, ui.props().IsEnabled, false);
    ui.focus(input);
    ui.key(Key::kDown);
    check(box->value() == 100, "NumberBox disabled ancestor blocks input");
    ui.set(id, ui.props().IsEnabled, true);
    auto source = createElement<NumberBox>();
    const auto sourceId = ui.insert(source);
    ui.bindElement(id, &NumberBox::valueProperty(), sourceId, &NumberBox::valueProperty(),
                   BindingMode::kTwoWay);
    source->setValue(7);
    box->stepValue(1);
    check(box->value() == 8 && source->value() == 8,
          "NumberBox TwoWay binding uses normalized actual properties");
    ui.tree().remove(id);
    const auto again = ui.insert(box);
    check(ui.templateChild(again, "InputBox") && !ui.tree().contains(input),
          "NumberBox reattachment rebuilds template and subscriptions");
    check(NumberBox::registerType(ui).empty(), "NumberBox source type registers with shared declaration");
    XamlIR ir;
    ir.sourceName = "number.xaml";
    ir.schemaFingerprint = schemaFingerprint();
    const auto type = ir.type(box->className());
    ir.types[type].contract = NumberBox::declaration().contract();
    const auto property = ir.property(NumberBox::valueProperty().id, NumberBox::valueProperty().name);
    const auto value = ir.value(PropertyValue(42.0));
    ir.instructions = {{XamlIROp::kBeginObject, type, 0, 0, 1, 1},
                       {XamlIROp::kSetValue, property, value, 0, 1, 2},
                       {XamlIROp::kEndObject, 0, 0, 0, 1, 3}};
    const auto loaded = loadComponent(ui, writeVxbf(ir));
    check(loaded.ok && std::dynamic_pointer_cast<NumberBox>(loaded.root)->value() == 42,
          "NumberBox compiled XAML instantiates actual internal class");
    ui.set(again, &NumberBox::valueProperty(), 1000.0);
    check(box->value() == 100 && box->text() == "100",
          "NumberBox Framework property writes normalize identically");
    Compositor retiringCompositor;
    Framework retiringUi(retiringCompositor);
    auto retiring = createElement<NumberBox>();
    const std::weak_ptr<NumberBox> weak = retiring;
    const auto retiringId = retiringUi.insert(retiring);
    retiring->addValueChanged(
        [&](f64, f64)
        {
            retiringUi.tree().remove(retiringId);
            retiring.reset();
        });
    const auto retiringInput = retiringUi.templateChild(retiringId, "InputBox");
    retiringUi.focus(retiringInput);
    retiringUi.textInput("5");
    retiringUi.key(Key::kEnter);
    check(weak.expired() && retiringUi.tree().node_count() == 0,
          "NumberBox commit callback may remove control and release last external reference");
}
} // namespace

int main(int argc, char** argv)
{
    initials();
    properties();
    numberProperties();
    if (argc == 2)
    {
        const auto error = mountFluent(argv[1]);
        if (!error.empty())
        {
            std::printf("FAIL: %s\n", error.c_str());
            return 1;
        }
        attached();
        numberAttached();
    }
    else
        std::puts("Fluent template checks: NOT_RUN (no compiled package supplied)");
    return failures ? 1 : 0;
}
