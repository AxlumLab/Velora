// M03 metadata test.
// usage: veloraMetadataTest <tests/rkmeta> <reference testdata> <src> [--update-golden]
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <velora/reflect/Metagen.hpp>
#include <velora/runtime/PropertyMetadata.hpp>

namespace fs = std::filesystem;
using namespace velora;
using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const std::string& name)
{
    std::printf("M03 %s: %s\n", name.c_str(), passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

std::string read(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read test input: " + path.string());
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (text.empty())
        throw std::runtime_error("empty test input: " + path.string());
    return text;
}

MetadataDocument generate(const fs::path& header, const fs::path& includeRoot, const fs::path& goldens,
                          bool update)
{
    RkParseOptions options;
    options.includeRoot = includeRoot;
    const auto source = read(header);
    const auto first = generateFromSource(source, header.filename().string(), options);
    const auto second = generateFromSource(source, header.filename().string(), options);
    const auto name = header.stem().string();
    check(first.result.ok() && second.result.ok() && first.result.text == second.result.text,
          name + " generates reproducibly");
    const auto golden = goldens / (name + ".rkmeta");
    if (update)
        std::ofstream(golden, std::ios::binary) << first.result.text;
    check(read(golden) == first.result.text, name + " matches golden " + golden.filename().string());
    const auto reparsed = parseMetadata(first.result.text);
    check(reparsed.ok() && serializeMetadata(reparsed.document) == first.result.text,
          name + " round-trips through parse");
    return first.result.document;
}

void schemaValidation(const std::string& valid)
{
    check(!parseMetadata("rkmeta\t2\n").ok(), "older schema rejected");
    check(!parseMetadata("rkmeta\t99\n").ok(), "newer schema rejected");
    check(!parseMetadata("rkmeta 3\n").ok(), "malformed header rejected");
    std::string tampered = valid;
    tampered.replace(tampered.find("velora::fluent::Button"), 22, "velora::fluent::Buttom");
    const auto badId = parseMetadata(tampered);
    check(!badId.ok() && badId.error.find("type id") != std::string::npos,
          "renamed type without new id rejected");
    check(!parseMetadata("rkmeta\t3\nfield\t1\tx\n").ok(), "member before type rejected");
    check(!parseMetadata(valid + "bogus\trow\n").ok(), "unknown row rejected");
    const auto truncated = valid.substr(0, valid.find("\nfield") + 10) + "\n";
    check(!parseMetadata(truncated).ok(), "truncated row rejected");
    const GeneratedUnit oldUnit{"old", 2, "rkmeta\t2\n"};
    check(!registerUnit(oldUnit), "unit from another generator version rejected");
}

void generatedUnit(const std::string& expected)
{
    const auto units = generatedUnits();
    const auto it = std::ranges::find_if(units, [](const auto* u)
                                         { return std::string(u->name) == "velora_metadata_test_controls"; });
    check(it != units.end() && (*it)->schemaVersion == kMetadataSchemaVersion && expected == (*it)->rkmeta,
          "build-generated controls unit is registered and matches the golden");
}

void bindings(const MetadataCatalog& catalog)
{
    const auto ok = [&](const char* type, const char* path, const char* resultType, bool writable)
    {
        const auto r = catalog.resolve(type, path);
        check(r.ok && r.type == resultType && r.writable == writable,
              std::string("bind ") + type + "." + path + " -> " + resultType);
    };
    const auto fails = [&](const char* type, const char* path, bool twoWay, const char* reason)
    {
        const auto r = catalog.resolve(type, path, twoWay);
        check(!r.ok && r.error.find(reason) != std::string::npos,
              std::string("reject ") + type + "." + path + " (" + r.error + ")");
    };
    ok("velora::fluent::Button", "Content", "std::string_view", false);
    ok("Button", "IsEnabled", "bool", true);
    ok("Slider", "Value", "float", true);
    ok("velora::fluent::Card", "radius", "float", true); // inherited from Panel
    ok("Card", "fill", "velora::Color", true);
    ok("CheckBox", "IsChecked", "bool", true);                          // inherited property
    ok("rkgen_test::Player", "position.x", "float", true);              // across documents
    ok("rkgen_test::nested::Inventory", "selected.count", "int", true); // through a pointer
    fails("Button", "Content", true, "read-only");
    fails("Slider", "Maximum", true, "read-only");
    fails("Button", "Missing", false, "no bindable member");
    fails("Card", "fill.r", false, "no reflection metadata");
    fails("NoSuchType", "x", false, "unknown type");
}

void ambiguousNames()
{
    MetadataCatalog catalog;
    catalog.add(
        generateFromSource(
            "namespace a { cstruct Item { int x; }; } namespace b { cstruct Item { int y; }; }", "ab.hpp")
            .result.document);
    check(!catalog.resolve("Item", "x").ok && catalog.resolve("a::Item", "x").ok &&
              catalog.resolve("b::Item", "y").ok,
          "ambiguous short type name needs qualification");
}

void uiRegistration(const MetadataDocument& controls)
{
    PropertyRegistry properties;
    EventRegistry events;
    std::size_t registered = 0;
    for (const auto& type : controls.types)
        registered += registerType(properties, events, type).properties.size();
    const auto* content = properties.find("velora::fluent::Button", "Content");
    const auto* enabled = properties.find("velora::fluent::Button", "IsEnabled");
    const auto* maximum = properties.find("velora::fluent::Slider", "Maximum");
    const auto* progress = properties.find("velora::fluent::ProgressBar", "Value");
    check(registered == 9, "9 control properties registered");
    check(content && content->kind == PropertyKind::kContent &&
              hasFlag(content->flags, PropertyFlags::kMeasure) && content->defaultValue.asString().empty(),
          "Button.Content is the Measure|Paint content property");
    check(enabled && enabled->defaultValue.asBool() && maximum && maximum->defaultValue.asDouble() == 1.0,
          "typed defaults");
    check(progress && progress != properties.find("velora::fluent::Slider", "Value"),
          "same name on two owners");
    const auto* click = events.find("velora::fluent::Button", "Click");
    check(click && click->signature == "void()" && events.find("velora::fluent::Slider", "ValueChanged"),
          "events registered");

    TypeMetadata bad;
    bad.qualifiedName = "demo::Bad";
    bad.properties.push_back({0, "Width", "demo::Bad::Width", "float", "width", "", "wide", {}, 1});
    bool rejected = false;
    try
    {
        registerType(properties, events, bad);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    check(rejected, "invalid default rejected");
}

void annotationDiagnostics()
{
    const auto errors = [](const char* source, const char* text)
    {
        const auto r = parseReflection(source);
        return !r.ok() && std::ranges::any_of(r.diagnostics, [&](const auto& d)
                                              { return d.message.find(text) != std::string::npos; });
    };
    check(errors("cclass A { rkproperty(Value, int) };", "needs get="), "rkproperty without getter");
    check(errors("cclass A { rkproperty(Value, int, get=v, Loud) };", "unknown rkproperty flag"),
          "unknown property flag");
    check(errors("cclass A { rkproperty(Value, int, get=v, bind=x) };", "unknown rkproperty key"),
          "unknown property key");
    check(errors("cclass A { rkproperty(Value) };", "needs a name and a type"), "property without type");
    check(errors("cclass A { rkevent(Click, void()) };", "needs add="), "event without adder");
    const auto access = generateFromSource(
        "namespace t { cclass P { cfield() int hidden; public: cfield() int shown; }; }", "t.hpp");
    MetadataCatalog catalog;
    catalog.add(access.result.document);
    check(catalog.resolve("t::P", "shown").ok && !catalog.resolve("t::P", "hidden").ok,
          "private fields are not bindable");
}

} // namespace

int runTests(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(
            stderr,
            "usage: velora_metadata_test <tests/rkmeta> <reference testdata> <src> [--update-golden]\n");
        return 2;
    }
    const fs::path goldens = argv[1], testdata = argv[2], src = argv[3];
    const bool update = argc > 4 && std::string(argv[4]) == "--update-golden";

    MetadataCatalog catalog;
    for (const char* name : {"BasicTypes.hpp", "Nested.hpp", "Player.hpp"})
        catalog.add(generate(testdata / name, testdata, goldens, update));
    const auto controls = generate(src / "controls.hpp", src, goldens, update);
    catalog.add(controls);
    const auto controlsText = serializeMetadata(controls);

    schemaValidation(controlsText);
    generatedUnit(controlsText);
    bindings(catalog);
    ambiguousNames();
    uiRegistration(controls);
    annotationDiagnostics();

    std::printf("M03 metadata exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    try
    {
        return runTests(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "metadata test: %s\n", error.what());
        return 1;
    }
}
