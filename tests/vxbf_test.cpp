// R06/R07 acceptance: the .vxbf format (round trip, corruption and version refusal, structure
// validation), same-named pages in different directories, and loading compiled pages into a UI
// context through the object runtime: names, events, resources, bindings, existing roots, missing
// metadata, and transactional rollback.
// R09: resource dictionaries (theme, merged, references), keyed/implicit/BasedOn styles, deferred
// ControlTemplates with TemplateBinding and per-instance name scopes, and the builder's diagnostics.
#include "Bound.g.hpp"
#include "Broken.g.hpp"
#include "Cycle.g.hpp"
#include "Listed.g.hpp"
#include "Localized.g.hpp"
#include "States.g.hpp"
#include "Styled.g.hpp"
#include "Themed.g.hpp"
#include "Views/A/Main.g.hpp"
#include "Views/B/Main.g.hpp"
#include "XBound.g.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <string>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/ElementSchema.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/controls/XamlItems.hpp>
#include <velora/data/Observable.hpp>
#include <velora/resources/ResourceIndex.hpp>
#include <velora/resources/ResourceProvider.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/xaml/XamlBinary.hpp>
#include <velora/xaml/XamlIRBuilder.hpp>
#include <velora/xaml/Parser.hpp>

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

bool rejects(std::vector<u8> bytes, std::string_view expected, u64 fingerprint = 0)
{
    const auto result = readVxbf(bytes, fingerprint);
    const auto* error = std::get_if<VxbfError>(&result);
    if (error && error->message.find(expected) == std::string::npos)
        std::printf("  (got: %s)\n", error->message.c_str());
    return error && error->message.find(expected) != std::string::npos;
}

void format()
{
    XamlIR ir;
    ir.sourceName = "Hand.xaml";
    ir.className = "Test.Hand";
    ir.schemaFingerprint = 0x1234;
    const u32 page = ir.type("Velora.UI.Page"), text = ir.type("Velora.UI.TextBlock");
    const u32 property = ir.property(propertyIdFor("", "Text"), "Text");
    ir.instructions = {{XamlIROp::kBeginObject, page, 0, 0, 1, 1},
                       {XamlIROp::kBeginObject, text, 0, 0, 2, 3},
                       {XamlIROp::kSetValue, property, ir.value(PropertyValue("hi")), 0, 2, 13},
                       {XamlIROp::kSetName, 0, ir.intern("Label"), 0, 2, 20},
                       {XamlIROp::kEndObject},
                       {XamlIROp::kEndObject}};
    const auto bytes = writeVxbf(ir);
    const auto read = readVxbf(bytes, 0x1234);
    const auto* back = std::get_if<XamlIR>(&read);
    check(back && back->instructions == ir.instructions && back->strings == ir.strings &&
              back->values == ir.values && back->className == ir.className,
          "a .vxbf round-trips its IR");
    check(writeVxbf(ir) == bytes, "writing is deterministic");

    auto corrupt = bytes;
    corrupt.back() ^= 0x40;
    check(rejects(corrupt, "checksum"), "a flipped byte is rejected (checksum)");
    check(rejects({bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2)}, "checksum"),
          "a truncated file is rejected");
    auto magic = bytes;
    magic[0] = 'X';
    check(rejects(magic, "magic"), "a foreign file is rejected (magic)");
    auto version = bytes;
    version[4] = 9;
    check(rejects(version, "unsupported version"), "another major version is rejected");
    check(rejects(bytes, "another element schema", 0x9999), "a file for another schema is rejected");

    XamlIR unbalanced = ir;
    unbalanced.instructions.pop_back();
    check(rejects(writeVxbf(unbalanced), "unbalanced"), "an unbalanced instruction stream is rejected");
    XamlIR outOfRange = ir;
    outOfRange.instructions[2].b = 99;
    check(rejects(writeVxbf(outOfRange), "out of range"), "an out-of-range index is rejected");
    XamlIR forged = ir;
    forged.types[1].id = typeIdFor("Velora.UI.Button");
    check(rejects(writeVxbf(forged), "type id does not match"),
          "a type id not derived from its name is rejected");

    // R09 block structure: every instruction only where its block allows it.
    const auto structure = [&](std::vector<XamlIRInstruction> body)
    {
        XamlIR shaped = ir;
        shaped.instructions = {{XamlIROp::kBeginObject, page}};
        shaped.instructions.insert(shaped.instructions.end(), body.begin(), body.end());
        shaped.instructions.push_back({XamlIROp::kEndObject});
        return validate(shaped);
    };
    const u32 key = ir.intern("Key");
    check(structure({{XamlIROp::kSetItemXBind, property, 0, 1}}).find("outside a DataTemplate") !=
              std::string::npos,
          "an item accessor outside a DataTemplate is rejected");
    check(structure({{XamlIROp::kSetter, property, 0}}).find("setter outside a style") != std::string::npos,
          "a setter outside a style is rejected");
    check(structure(
              {{XamlIROp::kBeginResources}, {XamlIROp::kSetValue, property, 0}, {XamlIROp::kEndResources}})
                  .find("SetValue outside an object") != std::string::npos,
          "a property value inside Resources is rejected");
    check(structure({{XamlIROp::kBeginResources},
                     {XamlIROp::kBeginTemplate, key, text},
                     {XamlIROp::kEndTemplate},
                     {XamlIROp::kEndResources}})
                  .find("exactly one root") != std::string::npos,
          "an empty template is rejected");
    check(structure({{XamlIROp::kSetTemplateBinding, property, property}}).find("outside a template") !=
              std::string::npos,
          "TemplateBinding outside a template is rejected");
    check(structure({{XamlIROp::kBeginResources},
                     {XamlIROp::kBeginStyle, kXamlNoIndex, text, kXamlNoIndex},
                     {XamlIROp::kSetter, property, 0},
                     {XamlIROp::kEndStyle},
                     {XamlIROp::kResource, key, 0},
                     {XamlIROp::kEndResources}})
              .empty(),
          "a well-formed Resources block validates");

    // Same-named pages in different directories compile to different files and URIs.
    check(VxbfTest::A::Main_component::uri().path() == "Views/A/Main.vxbf" &&
              VxbfTest::B::Main_component::uri().path() == "Views/B/Main.vxbf",
          "Views/A/Main.xaml and Views/B/Main.xaml have distinct outputs");
    const auto a = ResourceManager::instance().read(VxbfTest::A::Main_component::uri());
    const auto b = ResourceManager::instance().read(VxbfTest::B::Main_component::uri());
    check(a && b && *a != *b, "both pages are embedded");
    if (a)
    {
        const auto parsed = readVxbf(*a, schemaFingerprint());
        const auto* page = std::get_if<XamlIR>(&parsed);
        check(page && page->className == "VxbfTest.A.Main" && page->sourceName == "Views/A/Main.xaml",
              "the embedded page verifies against this build's schema");
        check(page && dump(*page).find("Grid.Row = 1") != std::string::npos &&
                  dump(*page).find("Click -> OnClick") != std::string::npos,
              "dump lists attached properties and event connections");
    }
}

void loading()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();

    VxbfTest::A::Main controllerA;
    VxbfTest::A::Main_component::Names namesA;
    const auto loadA = VxbfTest::A::Main_component::initializeComponent(ui, controllerA, namesA);
    check(loadA.ok, "page A loads");
    if (!loadA.ok)
    {
        std::printf("  %s\n", loadA.error.c_str());
        return;
    }
    check(loadA.root && loadA.root->is(ElementKind::kPage) && namesA.Title && namesA.Action,
          "the root and the x:Name objects are typed elements");
    check(namesA.Title->getValue(*p.Text).asString() == "Page A" &&
              namesA.Action->getValue(*p.GridRow).asInt() == 1,
          "literal and attached property values are set");
    const ElementId root = ui.tree().element(*loadA.root);
    ui.update(300, 200);
    const ElementId action = ui.tree().element(*namesA.Action);
    const Box box = ui.bounds(action);
    ui.pointerDown(box.x + 2, box.y + 2);
    ui.pointerUp(box.x + 2, box.y + 2);
    check(controllerA.clicks == 1, "Click is connected to the controller method");

    // Resources and bindings.
    ResourceDictionary dictionary;
    dictionary.values["CardBrush"] =
        ResourceValue{{PropertyValue(i64{0xFFFFFFFF}), PropertyValue(i64{0xFF202020})}};
    dictionary.values["CardPadding"] =
        ResourceValue{{PropertyValue(Thickness{6, 6, 6, 6}), PropertyValue(Thickness{6, 6, 6, 6})}};
    ui.resources().setApplication(std::move(dictionary));
    VxbfTest::Styled styled;
    VxbfTest::Styled_component::Names namesS;
    const auto loadS = VxbfTest::Styled_component::initializeComponent(ui, styled, namesS);
    check(loadS.ok, "a page with resources and bindings loads");
    if (loadS.ok)
    {
        check(namesS.Card->getValue(*p.Background).asInt() == 0xFFFFFFFF &&
                  namesS.Card->getValue(*p.Padding) == PropertyValue(Thickness{6, 6, 6, 6}),
              "{ThemeResource} and {StaticResource} resolve");
        ui.setTheme(Theme::kDark);
        check(namesS.Card->getValue(*p.Background).asInt() == 0xFF202020,
              "{ThemeResource} follows the theme");
        ui.setTheme(Theme::kLight);
        check(ui.bindingStats().bindings >= 2, "{Binding} expressions are attached");
    }

    // Transactional failure: nothing is left behind, the error names the source position.
    const usize nodes = ui.tree().node_count();
    VxbfTest::Broken broken;
    VxbfTest::Broken_component::Names namesBroken;
    const auto loadBroken = VxbfTest::Broken_component::initializeComponent(ui, broken, namesBroken);
    check(!loadBroken.ok && loadBroken.error.find("Broken.xaml:7:") != std::string::npos &&
              loadBroken.error.find("Missing") != std::string::npos,
          "a failing load reports the file, line and missing key");
    check(ui.tree().node_count() == nodes && !namesBroken.Before, "a failed load leaves no elements behind");

    // Loading into an existing root (x:Class instance): no second root is created.
    const ElementId existing = ui.create(ElementKind::kPage);
    const auto existingObject = std::static_pointer_cast<UIElement>(ui.tree().object(existing));
    VxbfTest::B::Main controllerB;
    VxbfTest::B::Main_component::Names namesB;
    ComponentLoadOptions into;
    into.existingRoot = existingObject.get();
    const usize before = ui.tree().node_count();
    const auto loadB = VxbfTest::B::Main_component::initializeComponent(ui, controllerB, namesB, into);
    check(loadB.ok && loadB.root == existingObject && ui.tree().children(existing).size() == 1 &&
              ui.tree().node_count() == before + 3,
          "loadComponent fills the existing root instead of creating one");
    check(namesB.Title && namesB.Title->getValue(*p.Text).asString() == "Page B" &&
              namesB.Toggle->getValue(*p.IsChecked).asBool(),
          "text content and literals load into the existing root's subtree");

    // A failing load into an existing root restores the root's own values.
    const ElementId target = ui.create(ElementKind::kPage);
    const auto targetObject = std::static_pointer_cast<UIElement>(ui.tree().object(target));
    ComponentLoadOptions intoTarget;
    intoTarget.existingRoot = targetObject.get();
    const auto failed = VxbfTest::Broken_component::initializeComponent(ui, broken, namesBroken, intoTarget);
    check(!failed.ok && !targetObject->readLocalValue(*p.Padding) && ui.tree().children(target).empty(),
          "a failed load into an existing root restores its values and removes its new children");
    ComponentLoadOptions wrongType;
    wrongType.existingRoot = ui.tree().uiElement(ui.create(ElementKind::kBorder));
    check(!VxbfTest::B::Main_component::initializeComponent(ui, controllerB, namesB, wrongType).ok,
          "an existing root of another type is refused");

    // Without reflection metadata a type cannot be loaded.
    RuntimeTypeRegistry empty;
    const auto bytes = ResourceManager::instance().read(VxbfTest::A::Main_component::uri());
    class NullTarget final : public XamlLoadTarget
    {
    public:
        u64 schemaFingerprint() const override
        {
            return velora::schemaFingerprint();
        }
        XamlHandle create(const XamlIRType&, XamlHandle, std::string&) override
        {
            return ++m_next;
        }
        bool isOfType(XamlHandle, const XamlIRType&) const override
        {
            return true;
        }
        std::string setValue(XamlHandle, const XamlIRProperty&, const PropertyValue&) override
        {
            return {};
        }
        std::string setBinding(XamlHandle, const XamlIRProperty&, std::string_view, u32,
                               const XamlBindingOptions&, XamlHandle) override
        {
            return {};
        }
        std::string setResource(XamlHandle, const XamlIRProperty&, std::string_view, bool) override
        {
            return {};
        }
        std::string addEvent(XamlHandle, std::string_view, std::function<void()>) override
        {
            return {};
        }
        void discard(XamlHandle) noexcept override {}
        std::optional<PropertyValue> localValue(XamlHandle, const XamlIRProperty&) const override
        {
            return std::nullopt;
        }
        void restoreLocal(XamlHandle, const XamlIRProperty&,
                          const std::optional<PropertyValue>&) noexcept override
        {
        }
        std::string setXBind(XamlHandle, const XamlIRProperty&, u32, u32) override
        {
            return {};
        }
        std::string applyUid(XamlHandle, std::string_view) override
        {
            return {};
        }
        std::string setItemTemplate(XamlHandle, std::string_view) override
        {
            return {};
        }
        std::string setItemTemplate(XamlHandle, const std::shared_ptr<const XamlIR>&, usize) override
        {
            return {};
        }
        std::string setItemsSource(XamlHandle, u32, u32) override
        {
            return {};
        }
        std::string setItemXBind(XamlHandle, const XamlIRProperty&, u32, u32) override
        {
            return {};
        }
        std::string setVisualStates(XamlHandle, XamlHandle, const XamlIR&, usize, usize,
                                    const std::unordered_map<std::string, XamlHandle>&) override
        {
            return {};
        }
        std::string setResources(XamlHandle, const std::shared_ptr<const XamlIR>&, usize, usize) override
        {
            return {};
        }
        void clearResources(XamlHandle) noexcept override {}
        std::string applyStyle(XamlHandle, std::string_view) override
        {
            return {};
        }
        std::string applyImplicitStyle(XamlHandle) override
        {
            return {};
        }
        std::string applyTemplate(XamlHandle, std::string_view) override
        {
            return {};
        }
        std::string applyTemplate(XamlHandle, const std::shared_ptr<const XamlIR>&, usize) override
        {
            return {};
        }
        std::string templateBind(XamlHandle, const XamlIRProperty&, XamlHandle,
                                 const XamlIRProperty&) override
        {
            return {};
        }
        void completeTemplate(XamlHandle, XamlHandle,
                              const std::unordered_map<std::string, XamlHandle>&) override
        {
        }

    private:
        XamlHandle m_next = 0;
    } nullTarget;
    const auto missing = XamlLoader(empty).load(*bytes, nullTarget);
    check(!missing.ok && missing.error.find("no reflection metadata") != std::string::npos,
          "a type without reflection metadata is an error");
    (void)root;
}

void styles()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    VxbfTest::Themed controller;
    VxbfTest::Themed_component::Names names;
    const auto load = VxbfTest::Themed_component::initializeComponent(ui, controller, names);
    check(load.ok, "a page with resources, styles and templates loads");
    if (!load.ok)
    {
        std::printf("  %s\n", load.error.c_str());
        return;
    }
    const auto id = [&](const std::shared_ptr<UIElement>& e) { return ui.tree().element(*e); };

    // Keyed styles, BasedOn, resource references, ThemeResource setters.
    check(names.Heading->getValue(*p.FontSize).asDouble() == 24 &&
              names.Heading->getValue(*p.Foreground).asInt() == 0xFF0078D4,
          "a keyed style applies; its StaticResource and ThemeResource setters resolve");
    check(names.Sub->getValue(*p.FontSize).asDouble() == 11 &&
              names.Sub->getValue(*p.Foreground).asInt() == 0xFF0078D4,
          "BasedOn inherits setters; a local value beats the style");
    names.Sub->clearValue(*p.FontSize);
    check(names.Sub->getValue(*p.FontSize).asDouble() == 15,
          "the derived style's setter shows once the local value is gone");

    // Implicit style with a template: one independent instance per Button.
    const ElementId first = id(names.First), second = id(names.Second), pill = id(names.Pill);
    check(names.First->getValue(*p.Padding) == PropertyValue(Thickness{8, 8, 8, 8}) &&
              names.Second->getValue(*p.Padding) == PropertyValue(Thickness{8, 8, 8, 8}),
          "an implicit style applies to every element of its type");
    const ElementId chrome1 = ui.templateChild(first, "Chrome"), chrome2 = ui.templateChild(second, "Chrome");
    check(chrome1 && chrome2 && chrome1 != chrome2,
          "each control gets its own template instance and name scope");
    check(ui.tree().property(chrome1, *p.Background).asInt() == 0xFF112233 &&
              ui.tree().property(chrome1, *p.Padding) == PropertyValue(Thickness{8, 8, 8, 8}),
          "TemplateBinding carries the control's values into its template");
    ui.set(first, p.Background, PropertyValue(i64{0xFF445566}));
    check(ui.tree().property(chrome1, *p.Background).asInt() == 0xFF445566 &&
              ui.tree().property(chrome2, *p.Background).asInt() != 0xFF445566,
          "TemplateBinding follows changes of its own control only");
    const ElementId presenter = ui.templateChild(first, "Presenter");
    check(presenter && ui.tree().property(presenter, *p.Content).asString() == "One",
          "a ContentPresenter shows the templated control's content");
    check(!load.names.contains("Chrome") && !load.names.contains("Presenter"),
          "template names stay out of the page's name scope");

    // OnApplyTemplate reached the controller for each templated Button, with its parts in place.
    // Pill's template is applied twice (the implicit style's, then Template=), so it is reported twice.
    check(controller.templated.size() == 4 && std::ranges::count(controller.templated, pill) == 2 &&
              std::ranges::count(controller.chromes, 0) == 0 &&
              std::ranges::find(controller.templated, second) != controller.templated.end() &&
              std::ranges::find(controller.templated, first) != controller.templated.end() &&
              std::ranges::find(controller.templated, pill) != controller.templated.end(),
          "OnApplyTemplate is called per templated element, and GetTemplateChild works in it");

    // An explicit template outranks the implicit style's; the style's setters still apply.
    const ElementId pillChrome = ui.templateChild(pill, "Chrome");
    check(pillChrome && ui.tree().property(pillChrome, *p.CornerRadius).asDouble() == 16 &&
              names.Pill->getValue(*p.Padding) == PropertyValue(Thickness{8, 8, 8, 8}),
          "Template= replaces the style's template, not its setters");
    ui.resources().applyStyle(pill, "Button");
    const ElementId reapplied = ui.templateChild(pill, "Chrome");
    check(reapplied && ui.tree().property(reapplied, *p.CornerRadius).asDouble() == 16,
          "applying the style again keeps the explicit template");

    // A merged ResourceDictionary file, referenced through the page's theme dictionaries.
    check(names.Shared->getValue(*p.Background).asInt() == 0xFF101010 &&
              names.Shared->getValue(*p.Padding) == PropertyValue(Thickness{3, 3, 3, 3}),
          "resources from a merged dictionary file resolve");
    check(names.Shared->getValue(*p.BorderBrush).asInt() == 0xFF101010,
          "a reference to a per-theme reference resolves in the referring theme");
    ui.setTheme(Theme::kDark);
    check(names.Shared->getValue(*p.BorderBrush).asInt() == 0xFFEFEFEF,
          "the chained reference follows the theme");
    check(names.Shared->getValue(*p.Background).asInt() == 0xFFEFEFEF &&
              names.Heading->getValue(*p.Foreground).asInt() == 0xFF60CDFF &&
              ui.tree().property(pillChrome, *p.Background).asInt() == 0xFF60CDFF,
          "ThemeResources follow the theme: aliases, style setters and template contents");
    // High contrast: its own dictionary where it has one, the dark value elsewhere.
    ui.setTheme(Theme::kHighContrast);
    check(names.Heading->getValue(*p.Foreground).asInt() == 0xFFFFFF00 &&
              ui.tree().property(pillChrome, *p.Background).asInt() == 0xFFFFFF00,
          "HighContrast uses the HighContrast dictionary");
    check(names.Shared->getValue(*p.Background).asInt() == 0xFFEFEFEF,
          "a resource without a HighContrast value falls back to the dark one");
    ui.setTheme(Theme::kLight);

    // Events on page elements still connect; the template replaced the Button's own chrome.
    ui.update(400, 400);
    const Box box = ui.bounds(first);
    ui.pointerDown(box.x + 3, box.y + 3);
    ui.pointerUp(box.x + 3, box.y + 3);
    check(controller.clicks == 1, "Click on a templated Button reaches the controller");

    // Templates are deferred and independent: 1000 more Buttons, 1000 more instances.
    const ElementId panel = id(names.Panel);
    std::vector<ElementId> chromes;
    for (int i = 0; i < 1000; ++i)
    {
        const ElementId button = ui.create(ElementKind::kButton, panel);
        ui.resources().applyStyle(button, "Button");
        chromes.push_back(ui.templateChild(button, "Chrome"));
    }
    std::ranges::sort(chromes);
    check(chromes.front() != 0 && std::ranges::adjacent_find(chromes) == chromes.end(),
          "1000 instantiations produce 1000 independent template trees");

    // Failure: a resource reference cycle is reported and nothing is left behind.
    const usize nodes = ui.tree().node_count();
    VxbfTest::Cycle cycle;
    VxbfTest::Cycle_component::Names cycleNames;
    const auto failed = VxbfTest::Cycle_component::initializeComponent(ui, cycle, cycleNames);
    check(!failed.ok && failed.error.find("cycle") != std::string::npos && ui.tree().node_count() == nodes,
          "a resource reference cycle fails the load and rolls it back");
}

// Upper-cases text (numbers are formatted first) and appends ConverterParameter; lower-cases back.
class Shout final : public IValueConverter
{
public:
    std::optional<PropertyValue> convert(const PropertyValue& value,
                                         std::string_view parameter) const override
    {
        std::string text = value.isString()   ? value.asString()
                           : value.isDouble() ? std::format("{}", value.asDouble())
                                              : std::string();
        if (!value.isString() && !value.isDouble())
            return std::nullopt;
        for (auto& c : text)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return PropertyValue(text + std::string(parameter));
    }
};

void bindings()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    VxbfTest::Bound controller;
    VxbfTest::Bound_component::Names names;
    const auto load = VxbfTest::Bound_component::initializeComponent(ui, controller, names);
    check(load.ok, "a page with ElementName, RelativeSource, Converter and FallbackValue bindings loads");
    if (!load.ok)
    {
        std::printf("  %s\n", load.error.c_str());
        return;
    }
    const auto id = [&](const std::shared_ptr<UIElement>& e) { return ui.tree().element(*e); };

    check(names.Mirror->getValue(*p.Text).asString() == "HELLO!",
          "ElementName (declared later) with a converter and its parameter");
    ui.set(id(names.Source), p.Text, PropertyValue(std::string("abc")));
    check(names.Mirror->getValue(*p.Text).asString() == "ABC!", "an ElementName binding follows its source");

    check(names.Twin->getValue(*p.Value).asDouble() == 30, "a TwoWay element binding takes the source value");
    ui.set(id(names.Twin), p.Value, PropertyValue(45.0));
    check(names.Volume->getValue(*p.Value).asDouble() == 45, "TwoWay writes the target's change back");
    ui.set(id(names.Volume), p.Value, PropertyValue(80.0));
    check(names.Twin->getValue(*p.Value).asDouble() == 50 &&
              names.Volume->getValue(*p.Value).asDouble() == 80,
          "the target coerces the pushed value without fighting its source");

    check(names.Self->getValue(*p.Text).asString() == "18",
          "RelativeSource Self reads the element's own property");
    check(names.Fallback->getValue(*p.Text).asString() == "n/a",
          "FallbackValue shows while there is no value");

    const ElementId labelled = id(names.Labelled);
    const ElementId caption = ui.templateChild(labelled, "Caption");
    check(caption && ui.tree().property(caption, *p.Text).asString() == "GO",
          "RelativeSource TemplatedParent inside a template instance");
    ui.set(labelled, p.Content, PropertyValue(std::string("stop")));
    check(ui.tree().property(caption, *p.Text).asString() == "STOP",
          "the template binding follows its control");

    // Cleanup: removing a source ends the bindings that read it.
    const usize before = ui.elementBindingCount();
    ui.tree().remove(id(names.Source));
    check(ui.elementBindingCount() == before - 1, "removing an element ends the bindings it takes part in");
}

void xbind()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    VxbfTest::XBound controller;
    VxbfTest::XBound_component::Names names;
    const auto load = VxbfTest::XBound_component::initializeComponent(ui, controller, names);
    check(load.ok, "a page with x:Bind loads");
    if (!load.ok)
    {
        std::printf("  %s\n", load.error.c_str());
        return;
    }
    const auto id = [&](const std::shared_ptr<UIElement>& e) { return ui.tree().element(*e); };
    auto& model = controller.model();
    check(names.Once->getValue(*p.Text).asString() == "first" &&
              names.Live->getValue(*p.Text).asString() == "first" &&
              names.Sized->getValue(*p.FontSize).asDouble() == 12,
          "x:Bind reads the compiled path");
    model.setTitle("second");
    check(names.Live->getValue(*p.Text).asString() == "second" &&
              names.Edit->getValue(*p.Text).asString() == "second",
          "OneWay and TwoWay x:Bind follow change notifications");
    check(names.Once->getValue(*p.Text).asString() == "first",
          "OneTime (the x:Bind default) does not follow");
    ui.set(id(names.Edit), p.Text, PropertyValue(std::string("typed")));
    check(model.title() == "typed" && names.Live->getValue(*p.Text).asString() == "typed",
          "TwoWay writes the target's change to the source");
    ui.set(id(names.Flag), p.IsChecked, PropertyValue(true));
    check(model.enabled(), "TwoWay x:Bind of a boolean");
    model.setSize(20);
    check(names.Sized->getValue(*p.FontSize).asDouble() == 20, "a numeric path converts to the target");

    // Cleanup: an element's x:Bind subscriptions end with it.
    const usize before = ui.accessorBindingCount();
    ui.tree().remove(id(names.Live));
    model.setTitle("after");
    check(ui.accessorBindingCount() == before - 1 && names.Edit->getValue(*p.Text).asString() == "after",
          "removing an element ends its x:Bind, the others continue");
}

void uid()
{
    // A string index with the x:Uid entries in two languages (what velorac compiles from .resw).
    struct Strings final : ResourceProvider
    {
        ResourceIndex table;
        std::optional<std::vector<u8>> read(const std::string&) const override
        {
            return std::nullopt;
        }
        bool contains(const std::string&) const override
        {
            return false;
        }
        const ResourceIndex* index() const override
        {
            return &table;
        }
    };
    auto strings = std::make_shared<Strings>();
    strings->table.name = "Test";
    strings->table.defaultLanguage = "en-US";
    strings->table.addString("Resources/Greeting.Text", "Hello", *parseQualifiers("lang-en-US"));
    strings->table.addString("Resources/Greeting.Text", "你好", *parseQualifiers("lang-zh-CN"));
    strings->table.addString("Resources/Greeting.FontSize", "20", *parseQualifiers("lang-zh-CN"));
    strings->table.addString("Resources/Ok.Content", "确定", *parseQualifiers("lang-zh-CN"));
    ResourceManager manager;
    manager.mount(ResourceNamespace::kApplication, strings);

    const auto loadIn = [&](std::string language, Framework& ui, VxbfTest::Localized_component::Names& names)
    {
        VxbfTest::Localized controller;
        ComponentLoadOptions options;
        options.resources = &manager;
        options.resourceContext.language = std::move(language);
        return VxbfTest::Localized_component::initializeComponent(ui, controller, names, options);
    };
    Compositor c1, c2;
    Framework english(c1), chinese(c2);
    const auto& p = english.props();
    VxbfTest::Localized_component::Names en, zh;
    const bool loaded = loadIn("en-US", english, en).ok && loadIn("zh-CN", chinese, zh).ok;
    check(loaded, "pages with x:Uid load");
    if (!loaded)
        return;
    check(en.Greeting->getValue(*p.Text).asString() == "Hello" &&
              en.Greeting->getValue(*p.FontSize).asDouble() == 10 &&
              en.Ok->getValue(*p.Content).asString() == "OK",
          "x:Uid applies the language's strings; properties it does not localize keep their markup values");
    check(zh.Greeting->getValue(*p.Text).asString() == "你好" &&
              zh.Greeting->getValue(*p.FontSize).asDouble() == 20 &&
              zh.Ok->getValue(*p.Content).asString() == "确定",
          "another root's language gets its own strings, converted to each property's type");

    strings->table.addString("Resources/Ok.Nonsense", "x", *parseQualifiers("lang-zh-CN"));
    Compositor c3;
    Framework broken(c3);
    VxbfTest::Localized_component::Names bad;
    const auto failed = loadIn("zh-CN", broken, bad);
    check(!failed.ok && failed.error.find("Ok.Nonsense") != std::string::npos,
          "an x:Uid string for a property the element lacks is an error");
}

// R10: visual states from compiled XAML; composition-only vs layout animations; two windows.
void states()
{
    Compositor compositor, otherCompositor;
    Framework ui(compositor), other(otherCompositor);
    const auto& p = ui.props();
    VxbfTest::States controller, otherController;
    VxbfTest::States_component::Names names, otherNames;
    const auto load = VxbfTest::States_component::initializeComponent(ui, controller, names);
    const bool loaded =
        load.ok && VxbfTest::States_component::initializeComponent(other, otherController, otherNames).ok;
    check(loaded, "a template with VisualStateManager loads");
    if (!loaded)
    {
        std::printf("  %s\n", load.error.c_str());
        return;
    }
    const ElementId button = ui.tree().element(*names.Target);
    const ElementId chrome = ui.templateChild(button, "Chrome"), label = ui.templateChild(button, "Label");
    check(chrome && label && ui.currentState(button, "CommonStates") == "Normal",
          "the states belong to the templated control, which starts in Normal");

    check(ui.goToState(button, "Disabled") && ui.tree().property(chrome, *p.Opacity).asDouble() == 1,
          "a generated transition starts from the current value");
    ui.advance(0.1);
    const double fading = ui.tree().property(chrome, *p.Opacity).asDouble();
    ui.advance(0.2);
    check(std::abs(fading - 0.7) < 0.01 && ui.tree().property(chrome, *p.Opacity).asDouble() == 0.4,
          "a VisualTransition animates a state setter over GeneratedDuration, then the setter holds");
    ui.goToState(button, "Normal");
    ui.advance(0.4);
    const double easedBack = ui.tree().property(chrome, *p.Opacity).asDouble();
    ui.advance(0.7); // past the end (0.6 - 0.2 is just under 0.4 in floating point)
    check(easedBack > 0.7 && easedBack < 1 && ui.tree().property(chrome, *p.Opacity).asDouble() == 1 &&
              !ui.tree().propertyAt(chrome, *p.Opacity, PropertySource::kAnimation).isDouble(),
          "the most specific transition eases a released value back, then lets go of it");
    ui.goToState(button, "Disabled");
    ui.advance(0.95); // the 0.2 s transition into Disabled has finished
    check(ui.tree().property(chrome, *p.Opacity).asDouble() == 0.4, "a state setter targets a template part");
    check(ui.goToState(button, "Pressed") && ui.tree().property(chrome, *p.Opacity).asDouble() == 1 &&
              ui.tree().property(label, *p.Foreground).asInt() == 0xFFFFE680,
          "leaving a state removes its setters; entering another applies its own");

    ui.advance(1.0); // the clock is absolute: start this state after the transitions above
    ui.goToState(button, "PointerOver");
    ui.advance(1.1);
    const i64 midway = ui.tree().property(chrome, *p.Background).asInt();
    ui.advance(1.2);
    check(midway != 0xFF000000 && midway != 0xFF005A9E &&
              ui.tree().property(chrome, *p.Background).asInt() == 0xFF005A9E,
          "a ColorAnimation interpolates to its ThemeResource target over its Duration");
    ui.setTheme(Theme::kDark);
    ui.advance(1.3);
    check(ui.tree().property(chrome, *p.Background).asInt() == 0xFF429CE3,
          "an active state's animation follows the theme");
    ui.setTheme(Theme::kLight);

    // Composition-only property: no layout invalidation while it animates.
    ui.update(400, 400);
    const auto dirty = [&]
    {
        bool any = false;
        for (const ElementId root : ui.tree().visualRoots())
            any = any || ui.tree().measureDirty(root) || ui.tree().arrangeDirty(root);
        return any;
    };
    ui.advance(1.0);
    ui.goToState(button, "Faded");
    ui.advance(1.25);
    const double faded = ui.tree().property(chrome, *p.Opacity).asDouble();
    check(faded > 0.49 && faded < 0.51 && !dirty(),
          "an Opacity animation runs From its start value without dirtying layout");

    // Layout property: the animation measures and arranges again.
    ui.update(400, 400);
    ui.goToState(button, "Wide");
    ui.advance(1.5);
    check(dirty(), "a Width animation invalidates layout");
    ui.update(400, 400);
    const float width = ui.bounds(chrome).width;
    ui.advance(2.0);
    ui.update(400, 400);
    check(width > 100 && width < 200 && ui.bounds(chrome).width == 200,
          "the layout animation is measured each frame");

    // Key frames: linear, discrete, eased, in KeyTime order.
    ui.goToState(button, "Plain");
    ui.advance(5.0);
    ui.goToState(button, "Pulse");
    const auto opacityAt = [&](double t)
    {
        ui.advance(5.0 + t);
        return ui.tree().property(chrome, *p.Opacity).asDouble();
    };
    const double linearHalf = opacityAt(0.05), beforeJump = opacityAt(0.19), afterJump = opacityAt(0.21),
                 easedHalf = opacityAt(0.3), end = opacityAt(0.5);
    check(std::abs(linearHalf - 0.75) < 0.01 && std::abs(beforeJump - 0.5) < 0.01 &&
              std::abs(afterJump - 0.3) < 0.01 && easedHalf > 0.3 && easedHalf < 0.65 &&
              std::abs(end - 1) < 0.01,
          "a key frame animation runs its linear, discrete and eased frames in order");
    ui.goToState(button, "Plain");

    // Another window's instance is untouched.
    const ElementId otherChrome = other.templateChild(other.tree().element(*otherNames.Target), "Chrome");
    check(other.tree().property(otherChrome, *p.Opacity).asDouble() == 1 &&
              other.currentState(other.tree().element(*otherNames.Target), "Custom") != "Wide",
          "states of one window do not leak into another");
}

// R09.5: DataTemplate items through the .vxbf pipeline.
void items()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto& p = ui.props();
    VxbfTest::Listed controller;
    for (const char* name : {"Ada", "Bob", "Cy", "Dee", "Eve"})
        controller.people().emplace_back(name);
    VxbfTest::Listed_component::Names names;
    const auto load = VxbfTest::Listed_component::initializeComponent(ui, controller, names);
    check(load.ok, "a page with ListView DataTemplates loads");
    if (!load.ok)
    {
        std::printf("  %s\n", load.error.c_str());
        return;
    }
    ui.update(300, 300);
    const ElementId list = ui.tree().element(*names.People), keyed = ui.tree().element(*names.Keyed);
    // The visible containers (Visibility true) and their texts, in item order.
    const auto shown = [&](ElementId l)
    {
        std::vector<std::pair<i64, std::string>> items;
        for (const ElementId c : ui.tree().children(l))
            if (ui.tree().property(c, *p.Visibility).asBool())
                items.emplace_back(ui.tree().property(c, *p.ItemIndex).asInt(),
                                   ui.tree().property(c, *p.Text).asString());
        std::ranges::sort(items);
        std::vector<std::string> texts;
        for (auto& [i, t] : items)
            texts.push_back(t);
        return texts;
    };
    check(shown(list) == std::vector<std::string>{"Ada", "Bob", "Cy"},
          "the visible items are realized from an inline DataTemplate with {Binding} to each item");
    check(shown(keyed) == std::vector<std::string>{"Ada", "Bob", "Cy"} &&
              ui.tree().property(ui.tree().children(keyed).front(), *p.FontSize).asDouble() == 20,
          "a keyed DataTemplate resource serves ItemTemplate");
    check(!load.names.contains("Label"), "names inside a DataTemplate stay out of the page's scope");

    controller.people()[1].setName("Bea");
    check(shown(list)[1] == "Bea", "an item's change notification updates its container");
    const ElementId once = ui.tree().element(*names.Once);
    check(shown(keyed)[1] == "Bea" && shown(once)[1] == "Bob",
          "keyed item TwoWay follows notifications while OneTime keeps its initial value");
    auto keyedHost = XamlItemsHost::find(ui, keyed);
    ui.set(keyedHost->container(1), p.Text, PropertyValue("Betty"));
    check(controller.people()[1].name() == "Betty" && shown(list)[1] == "Betty",
          "item TwoWay writes to the current item and updates another list");
    controller.people()[1].setName("Bea");
    ui.scrollTo(once, 40);
    ui.update(300, 300);
    check(shown(once) == std::vector<std::string>{"Cy", "Dee", "Eve"},
          "OneTime item accessors read the new item when a container is recycled");
    controller.people().emplace(0, "Abe");
    ui.update(300, 300);
    check(shown(list) == std::vector<std::string>{"Abe", "Ada", "Bea"},
          "an insertion shifts the realized items");
    controller.people().erase(1);
    ui.update(300, 300);
    check(shown(list) == std::vector<std::string>{"Abe", "Bea", "Cy"}, "a removal shifts them back");
    const usize containers = ui.tree().children(list).size();
    check(containers <= 3 + XamlItemsHost::kMaxPool, "only the visible items have containers (virtualized)");
    ui.tree().remove(ui.tree().element(*load.root));
    check(!XamlItemsHost::find(ui, keyed) && controller.people().subscriberCount() == 0,
          "removing a list unregisters its host and releases collection subscriptions");
}

std::string diagnose(std::string_view source)
{
    const auto parsed = parseXaml(source);
    if (!parsed.ok())
        return parsed.diagnostics.front().message;
    const auto built = buildXamlIR(parsed.document, "Test.xaml");
    return built.ok() ? std::string() : built.diagnostics.front().message;
}

void diagnostics()
{
    const std::string head = R"(<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                                      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">)";
    check(diagnose(head + R"(<Border Background="{TemplateBinding Background}"/></Page>)")
                  .find("only valid inside a ControlTemplate") != std::string::npos,
          "TemplateBinding outside a template is a diagnostic");
    check(diagnose(head + R"(<Page.Resources><ResourceDictionary><ResourceDictionary.ThemeDictionaries>
                             <ResourceDictionary x:Key="Purple"/></ResourceDictionary.ThemeDictionaries>
                             </ResourceDictionary></Page.Resources></Page>)")
                  .find("Purple") != std::string::npos,
          "an unknown theme dictionary key is reported, not dropped");
    check(diagnose(head + R"(<Page.Resources><Style x:Key="S"><Setter Property="FontSize" Value="2"/></Style>
                             </Page.Resources></Page>)")
                  .find("TargetType") != std::string::npos,
          "a Style without a TargetType is a diagnostic");
    check(diagnose(head + R"(<Page.Resources><Style x:Key="S" TargetType="TextBlock">
                             <Setter Property="Padding" Value="2"/></Style></Page.Resources></Page>)")
                  .find("has no property 'Padding'") != std::string::npos,
          "a setter for a property the target type lacks is a diagnostic");
    check(diagnose(head + R"(<Page.Resources><ControlTemplate x:Key="T" TargetType="Button">
                             <Border Background="{TemplateBinding Text}"/></ControlTemplate></Page.Resources></Page>)")
                  .find("has no property 'Text'") != std::string::npos,
          "a TemplateBinding to a property the templated type lacks is a diagnostic");
    check(diagnose(head + R"(<Page.Resources><ControlTemplate x:Key="T" TargetType="Button">
                             <Border><VisualStateManager.VisualStateGroups><VisualStateGroup><VisualState x:Name="S">
                             <VisualState.Setters><Setter Target="Nobody.Opacity" Value="1"/></VisualState.Setters>
                             </VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Border>
                             </ControlTemplate></Page.Resources></Page>)")
                  .find("'Nobody' is not an x:Name") != std::string::npos,
          "visual state targets must be names of the template");
    check(
        diagnose(head +
                 R"(<StackPanel><TextBlock Text="{Binding Text, ElementName=Nobody}"/></StackPanel></Page>)")
                .find("not an x:Name") != std::string::npos,
        "an ElementName that names nothing is a diagnostic");
    check(diagnose(head +
                   R"(<StackPanel><TextBlock x:Name="A"/><TextBlock Text="{Binding Value, ElementName=A}"/>
                             </StackPanel></Page>)")
                  .find("has no property 'Value'") != std::string::npos,
          "an ElementName path the element's type lacks is a diagnostic");
    check(
        diagnose(
            head +
            R"(<TextBlock Text="{Binding Content, RelativeSource={RelativeSource TemplatedParent}}"/></Page>)")
                .find("only valid inside a ControlTemplate") != std::string::npos,
        "TemplatedParent outside a template is a diagnostic");
    check(diagnose(head + R"(<TextBlock Text="{Binding Name, TargetNullValue=x}"/></Page>)")
                  .find("TargetNullValue") != std::string::npos,
          "unsupported Binding options are reported, not dropped");
    check(diagnose(head + R"(<TextBlock Text="{x:Bind Title}"/></Page>)").find("reflection metadata") !=
              std::string::npos,
          "x:Bind without the x:Class's metadata is a diagnostic");
    check(diagnose(head + R"(<ListView><ListView.ItemTemplate><DataTemplate>
                  <TextBlock Text="{x:Bind Name}"/></DataTemplate></ListView.ItemTemplate></ListView></Page>)")
                  .find("needs x:DataType") != std::string::npos,
          "item x:Bind without x:DataType is a diagnostic");
    check(diagnose(head + R"(<ListView><ListView.ItemTemplate><DataTemplate>
                  <Button Click="Remove"/></DataTemplate></ListView.ItemTemplate></ListView></Page>)")
                  .find("needs x:DataType") != std::string::npos,
          "item events without x:DataType are a diagnostic");
    check(diagnose(R"(<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                      xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" xmlns:local="using:App">
                      <Page.Resources>
                      <DataTemplate x:Key="A" x:DataType="local:Person"><Button Click="Remove"/></DataTemplate>
                      <DataTemplate x:Key="B" x:DataType="local:Group"><Button Click="Remove"/></DataTemplate>
                      </Page.Resources></Page>)")
                  .find("different item types") != std::string::npos,
          "ambiguous item handler types are diagnosed instead of silently dropping a handler");
}
} // namespace

int main()
{
    format();
    loading();
    registerValueConverter("VxbfTest.Shout", [] { return std::make_shared<Shout>(); });
    styles();
    bindings();
    xbind();
    uid();
    states();
    items();
    diagnostics();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
