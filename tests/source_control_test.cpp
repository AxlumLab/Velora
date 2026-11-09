#include "../examples/custom_control/ValuePad.hpp"
#include "ValuePadTypes.hpp"

#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <velora/composition/Compositor.hpp>
#include <velora/controls/ElementFactory.hpp>
#include <velora/controls/ElementSchema.hpp>
#include <velora/controls/XamlComponent.hpp>
#include <velora/xaml/XamlBinary.hpp>

#ifdef VELORA_TEST_SOURCE_XAML
#include "SourcePage.g.hpp"
#include <velora/xaml/Parser.hpp>
#include <velora/xaml/XamlIRBuilder.hpp>
#endif

using namespace velora;

namespace
{
int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("source controls %s: %s\n", name, passed ? "PASS" : "FAIL");
    failures += !passed;
}

XamlIR page(PropertyValue value = PropertyValue(30.0))
{
    XamlIR ir;
    ir.sourceName = "SourcePage.xaml";
    ir.schemaFingerprint = schemaFingerprint();
    const auto root = ir.type("Velora.UI.Page");
    const auto control = ir.type(ValuePad::declaration().className);
    ir.types[control].contract = ValuePad::declaration().contract();
    const auto width = ir.property(elementProperties().props.Width->id, "Width");
    const auto property = ir.property(ValuePad::valueProperty().id, ValuePad::valueProperty().name);
    const auto amount = ir.value(std::move(value));
    const auto rootWidth = ir.value(PropertyValue(200.0));
    const auto name = ir.intern("Pad");
    ir.instructions = {{XamlIROp::kBeginObject, root, 0, 0, 1, 1},
                       {XamlIROp::kSetValue, width, rootWidth, 0, 1, 5},
                       {XamlIROp::kBeginObject, control, 0, 0, 2, 1},
                       {XamlIROp::kSetName, 0, name, 0, 2, 5},
                       {XamlIROp::kSetValue, property, amount, 0, 2, 12},
                       {XamlIROp::kEndObject, 0, 0, 0, 2, 30},
                       {XamlIROp::kEndObject, 0, 0, 0, 3, 1}};
    return ir;
}

void declarations()
{
    XamlTypeCatalog catalog;
    check(catalog.append(kValuePadTypes).empty() && catalog.types().size() == 1,
          "compiler/runtime declaration parses");
    const auto contract = catalog.types()[0].contract();
    check(!catalog.append(kValuePadTypes).empty() && catalog.types().size() == 1 &&
              catalog.types()[0].contract() == contract,
          "duplicate append is transactional");
    check(!catalog.append("velora-xaml-types\t1\ntype\tDemo.Broken\tMissing\nend\n").empty() &&
              !catalog.find("Demo.Broken"),
          "unknown bases do not publish types");
    auto broken = std::string(kValuePadTypes);
    broken.replace(broken.find("Double"), 6, "Mystery");
    XamlTypeCatalog invalid;
    check(!invalid.append(broken).empty() && invalid.types().empty(), "unknown property kinds are diagnosed");
    auto changed = ValuePad::declaration();
    changed.properties[0].defaultValue = "5";
    check(changed.contract() != contract, "property changes alter the referenced type contract");
}

void runtime()
{
    Compositor compositor;
    Framework ui(compositor);
    const auto bytes = writeVxbf(page());
    check(bytes[6] == 4, "source controls use the 1.4 type-contract format");
    const auto read = readVxbf(bytes, schemaFingerprint());
    check(std::holds_alternative<XamlIR>(read) &&
              std::get<XamlIR>(read).types[1].contract == ValuePad::declaration().contract(),
          "type contracts survive binary round-trip");
    const auto missing = loadComponent(ui, bytes);
    check(!missing.ok && missing.error.find("Demo.ValuePad") != std::string::npos &&
              ui.tree().node_count() == 0,
          "missing factory rolls back the created page");
    check(ValuePad::registerType(ui).empty(), "source factory and property descriptors register");
    const auto count = ui.sourceControls().types().size();
    check(!ValuePad::registerType(ui).empty() && ui.sourceControls().types().size() == count,
          "duplicate registration leaves the runtime unchanged");
    auto result = loadComponent(ui, bytes);
    auto pad = result.ok ? std::dynamic_pointer_cast<ValuePad>(result.names.at("Pad")) : nullptr;
    check(pad && pad->value() == 30 && pad->className() == "Demo.ValuePad" &&
              pad->typeId() == typeIdFor("Demo.ValuePad"),
          "compiled identities create the actual source control");
    if (result.ok)
        ui.tree().remove(ui.tree().element(*result.root));

    struct Unrelated final : UIElement
    {
        Unrelated() : UIElement(ElementKind::kUserControl)
        {
        }
        std::string className() const override
        {
            return "Demo.Unrelated";
        }
    };
    const XamlTypeDeclaration unrelated{"Demo.Unrelated", "UserControl", {}};
    check(ui.registerSourceControl(unrelated, [] { return createElement<Unrelated>(); }, {}).empty(),
          "unrelated source control registers without a new built-in enum");
    result = loadComponent(ui, bytes);
    check(result.ok, "adding an unrelated type preserves the existing compiled page contract");
    if (result.ok)
        ui.tree().remove(ui.tree().element(*result.root));

    auto wrong = page();
    ++wrong.types[1].contract;
    result = loadComponent(ui, writeVxbf(wrong));
    check(!result.ok && result.error.find("contract mismatch") != std::string::npos &&
              ui.tree().node_count() == 0,
          "mismatched type contracts roll back");
    result = loadComponent(ui, writeVxbf(page(PropertyValue(true))));
    check(!result.ok && result.error.find("SourcePage.xaml:2:12") != std::string::npos &&
              ui.tree().node_count() == 0,
          "invalid source values have a source location and roll back");

    const auto root = ui.create(ElementKind::kPage);
    ui.set(root, ui.props().Width, 150.0);
    ComponentLoadOptions options;
    options.existingRoot = ui.tree().uiElement(root);
    result = loadComponent(ui, writeVxbf(page(PropertyValue(true))), options);
    check(!result.ok && ui.tree().node_count() == 1 && ui.tree().children(root).empty() &&
              ui.tree().property(root, *ui.props().Width).asDouble() == 150,
          "failure restores an existing root and removes new children");
    ui.tree().remove(root);

    Compositor otherCompositor;
    Framework other(otherCompositor);
    const std::array descriptors = {&ValuePad::valueProperty()};
    check(other
              .registerSourceControl(
                  ValuePad::declaration(), []() -> std::shared_ptr<UIElement>
                  { throw std::runtime_error("factory failure"); }, descriptors)
              .empty(),
          "throwing factory registers");
    result = loadComponent(other, bytes);
    check(!result.ok && result.error.find("factory failure") != std::string::npos &&
              other.tree().node_count() == 0,
          "factory exceptions become load errors with rollback");

    Compositor reentrantCompositor;
    Framework reentrant(reentrantCompositor);
    reentrant.registerSourceControl(
        ValuePad::declaration(),
        [&]
        {
            const auto error =
                reentrant.registerSourceControl(unrelated, [] { return createElement<Unrelated>(); }, {});
            if (!error.empty())
                throw std::runtime_error(error);
            return createElement<ValuePad>();
        },
        descriptors);
    result = loadComponent(reentrant, bytes);
    check(result.ok && reentrant.sourceControls().contains("Demo.Unrelated"),
          "factory-time registration does not invalidate the active factory or declaration");
}

#ifdef VELORA_TEST_SOURCE_XAML
void compiler()
{
    XamlTypeCatalog catalog;
    catalog.append(kValuePadTypes);
    const std::string prefix = R"(<Page xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" xmlns:local="using:Demo" x:Class="Demo.SourcePage">)";
    const auto checkBad = [&](const std::string& object)
    {
        const auto parsed = parseXaml(prefix + object + "</Page>");
        const auto result = buildXamlIR(parsed.document, "negative.xaml", nullptr, &catalog);
        return parsed.ok() && !result.ok() && result.diagnostics[0].span.begin.line != 0;
    };
    check(checkBad("<local:Missing/>"), "unknown source types retain diagnostics");
    check(checkBad("<local:ValuePad Missing='1'/>"), "unknown source properties retain diagnostics");
    check(checkBad("<local:ValuePad Value='wrong'/>"), "invalid literals retain diagnostics");
    check(checkBad("<local:ValuePad Value='{Binding Amount}'/>"),
          "unsupported source bindings are explicit diagnostics");
    Compositor compositor;
    Framework ui(compositor);
    ValuePad::registerType(ui);
    Demo::SourcePage controller;
    Demo::SourcePage_component::Names names;
    const auto result = Demo::SourcePage_component::initializeComponent(ui, controller, names);
    const auto pad = std::dynamic_pointer_cast<ValuePad>(names.Pad);
    check(result.ok && pad && pad->value() == 30,
          "velorac-generated component loads its declared source control");
}
#endif
} // namespace

int main()
{
    declarations();
    runtime();
#ifdef VELORA_TEST_SOURCE_XAML
    compiler();
#endif
    return failures ? 1 : 0;
}
