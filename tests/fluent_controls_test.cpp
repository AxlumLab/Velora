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
#include <velora/fluent/PersonPicture.hpp>
#include <velora/graphics/DisplayList.hpp>
#include <velora/xaml/XamlBinary.hpp>

using namespace velora;
namespace
{
int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("Fluent PersonPicture %s: %s\n", name, passed ? "PASS" : "FAIL");
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
} // namespace

int main(int argc, char** argv)
{
    initials();
    properties();
    if (argc == 2)
    {
        const auto error = mountFluent(argv[1]);
        if (!error.empty())
        {
            std::printf("FAIL: %s\n", error.c_str());
            return 1;
        }
        attached();
    }
    else
        std::puts("Fluent template checks: NOT_RUN (no compiled package supplied)");
    return failures ? 1 : 0;
}
