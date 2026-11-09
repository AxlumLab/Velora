// M08 velorac XAML compiler test.
// usage: veloracTest <tests/velorac> [--update-golden]
//   Controller.hpp  controllers, reflected in-process into .rkmeta as `velorac reflect` does
//   peers/*.xaml    custom control views visible to every compilation
//   valid/*.xaml    compile cleanly; output is deterministic and equals <name>.g.hpp/.g.cpp.golden
//   invalid/*.xaml  rejected without output; every "<line>:<column> <message>" line of
//                   <name>.expect matches a diagnostic
//   ../xaml/winui/*.xaml  the unmodified WinUI 2.3 dictionaries, checked as resources with the
//                   M10 platform stand-ins as application resources: the diagnostics (what the
//                   Velora subset cannot map) equal winui/<name>.diagnostics
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <velora/reflect/Metagen.hpp>
#include <velora/xaml/Compiler.hpp>
#include <velora/xaml/Parser.hpp>

namespace fs = std::filesystem;
using namespace velora;

namespace
{

int failures = 0;
bool update = false;

void check(bool passed, const std::string& name)
{
    if (!passed)
    {
        std::printf("FAIL %s\n", name.c_str());
        ++failures;
    }
}

std::string read(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<fs::path> files(const fs::path& dir)
{
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".xaml")
            out.push_back(e.path());
    std::ranges::sort(out);
    return out;
}

std::string diagnostics(const CompileResult& r, const std::string& name)
{
    std::string out;
    for (const auto& d : r.diagnostics)
        out += "\n    " + velora::formatDiagnostic(d, name);
    return out;
}

// #line directives carry the path as given; a path relative to the test root keeps goldens
// independent of the checkout location.
CompileResult compileFile(const fs::path& root, const fs::path& file, const CompileOptions& options)
{
    return compile(read(file), fs::relative(file, root), options);
}

void golden(const fs::path& path, const std::string& text)
{
    if (update)
        std::ofstream(path, std::ios::binary) << text;
    check(read(path) == text, path.filename().string() + " matches golden");
}

void valid(const fs::path& root, const CompileOptions& options)
{
    for (const auto& file : files(root / "valid"))
    {
        const auto name = file.filename().string();
        const auto result = compileFile(root, file, options);
        check(result.ok(), name + " compiles" + diagnostics(result, name));
        if (!result.ok())
            continue;
        const auto again = compileFile(root, file, options);
        check(again.header == result.header && again.source == result.source,
              name + " output is deterministic");
        auto reversed = options;
        std::ranges::reverse(reversed.metadata);
        std::ranges::reverse(reversed.peers);
        const auto reordered = compileFile(root, file, reversed);
        check(reordered.header == result.header && reordered.source == result.source,
              name + " output ignores option order");
        const auto stem = file.stem().string();
        golden(file.parent_path() / (stem + ".g.hpp.golden"), result.header);
        golden(file.parent_path() / (stem + ".g.cpp.golden"), result.source);
        std::printf("valid %s: %zu header bytes, %zu source bytes, %zu dependencies\n", name.c_str(),
                    result.header.size(), result.source.size(), result.dependencies.size());
    }
}

void invalid(const fs::path& root, const CompileOptions& options)
{
    for (const auto& file : files(root / "invalid"))
    {
        const auto name = file.filename().string();
        const auto result = compileFile(root, file, options);
        check(!result.ok(), name + " is rejected");
        check(result.header.empty() && result.source.empty(), name + " produces no output");
        std::istringstream expect(read(fs::path(file).replace_extension(".expect")));
        std::size_t expectations = 0;
        for (std::string line; std::getline(expect, line);)
        {
            if (line.empty())
                continue;
            ++expectations;
            const auto colon = line.find(':'), space = line.find(' ');
            const auto wantLine = std::stoul(line.substr(0, colon));
            const auto wantColumn = std::stoul(line.substr(colon + 1, space - colon - 1));
            const auto message = line.substr(space + 1);
            const bool found = std::ranges::any_of(result.diagnostics,
                                                   [&](const XamlDiagnostic& d)
                                                   {
                                                       return d.span.begin.line == wantLine &&
                                                              d.span.begin.column == wantColumn &&
                                                              d.message.find(message) != std::string::npos;
                                                   });
            check(found, name + " reports '" + line + "'" + diagnostics(result, name));
        }
        check(expectations > 0, name + " has expectations");
        std::printf("invalid %s:%s\n", name.c_str(), diagnostics(result, name).c_str());
    }
}

// Unmodified WinUI dictionaries: every resource resolves; only unmappable semantics remain.
void winui(const fs::path& root, const fs::path& repository)
{
    CompileOptions options;
    for (const char* file : {"SystemResources.xaml", "WinUICornerRadius.xaml"})
        options.application.push_back(repository / "examples" / "m10_styles" / file);
    for (const auto& file : files(repository / "tests" / "xaml" / "winui"))
    {
        const auto name = file.filename().string();
        const auto result = checkResources(read(file), file.filename(), options);
        std::string text;
        for (const auto& d : result.diagnostics)
            text += std::to_string(d.span.begin.line) + ":" + std::to_string(d.span.begin.column) + " " +
                    d.message + "\n";
        golden(root / "winui" / (file.stem().string() + ".diagnostics"), text);
        bool undefined = false;
        for (const auto& d : result.diagnostics)
            undefined = undefined || d.message.find("is not defined") != std::string::npos;
        check(!undefined, name + " resolves every resource reference");
        std::printf("winui %s: %zu diagnostics\n", name.c_str(), result.diagnostics.size());
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: velorac_test <tests/velorac> [--update-golden]\n");
        return 2;
    }
    const fs::path root = fs::absolute(argv[1]);
    update = argc > 2 && std::string(argv[2]) == "--update-golden";

    // Reflect the controllers and hand the .rkmeta to the compiler, as the build does.
    const auto header = root / "Controller.hpp";
    const auto generated = velora::generateFromSource(read(header), header.string());
    check(generated.result.ok(), "Controller.hpp reflects: " + generated.result.error);
    const auto meta = fs::temp_directory_path() / "velorac_test_Controller.rkmeta";
    std::ofstream(meta, std::ios::binary) << generated.result.text;

    CompileOptions options;
    options.metadata.emplace_back(meta, "Controller.hpp");
    options.peers = files(root / "peers");
    for (const auto& peer : options.peers)
    {
        const auto source = read(peer);
        const bool dictionary = source.find("<ResourceDictionary") == source.find_first_of('<');
        const auto result = dictionary ? checkResources(source, fs::relative(peer, root), options)
                                       : compileFile(root, peer, options);
        check(result.ok(),
              peer.filename().string() + " compiles" + diagnostics(result, peer.filename().string()));
    }

    valid(root, options);
    const auto customResources = compile(R"(<Page x:Class="Test.Page"
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" xmlns:local="using:Test">
        <local:Chip><local:Chip.Resources><x:Double x:Key="ChildGap">12</x:Double></local:Chip.Resources></local:Chip>
        </Page>)",
                                         "CustomResources.xaml", options);
    check(customResources.ok() &&
              customResources.source.find("setScope(m_elements[1]") != std::string::npos &&
              customResources.source.find("values[\"ChildGap\"]") != std::string::npos,
          "custom control resources publish on its actual native root" +
              diagnostics(customResources, "CustomResources.xaml"));
    const auto styleResources = compileFile(root, root / "valid" / "Styles.xaml", options);
    check(
        styleResources.ok() &&
            styleResources.source.find(".templateFactory = [](velora::Framework& ui") != std::string::npos &&
            styleResources.source.find("std::make_shared<::Test::Page_xaml::ControlTemplate0>(ui, owner)") !=
                std::string::npos,
        "compiled template style registers a captureless factory");
    invalid(root, options);
    winui(root, root.parent_path().parent_path());
    for (const auto& types : {std::pair{"i64", "f32"}, std::pair{"velora::u32", "velora::f64"}})
    {
        std::string aliases = read(header);
        const auto replace = [&](std::string_view before, std::string_view after)
        {
            for (auto pos = aliases.find(before); pos != std::string::npos;
                 pos = aliases.find(before, pos + after.size()))
                aliases.replace(pos, before.size(), after);
        };
        replace("Count, int", std::string("Count, ") + types.first);
        replace("int count()", std::string(types.first) + " count()");
        replace("Ratio, double", std::string("Ratio, ") + types.second);
        replace("double ratio()", std::string(types.second) + " ratio()");
        const auto reflected = generateFromSource(aliases, header.string());
        check(reflected.result.ok(), "project aliases reflect");
        std::ofstream(meta, std::ios::binary) << reflected.result.text;
        const auto result = compile(R"(<Page x:Class="Test.Page"
            xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
            xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
            <TextBlock Grid.Column="{x:Bind Count}" Width="{x:Bind Ratio}"/>
            </Page>)",
                                    "Aliases.xaml", options);
        check(result.ok() && result.source.find("static_cast<velora::i64>") != std::string::npos,
              "project aliases compile into integer and floating property bindings" +
                  diagnostics(result, "Aliases.xaml"));
    }
    fs::remove(meta);
    std::printf("velorac_test failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
