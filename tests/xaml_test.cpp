#include <velora/globals.hpp>
// M07 XAML front end test.
// usage: veloraXamlTest <tests/xaml> [--update-golden]
//   valid/*.xaml    parse cleanly; dump equals <name>.golden; reparse and the CRLF variant agree
//   invalid/*.xaml  every "<line>:<column> <message>" line of <name>.expect matches a diagnostic
//   winui/*.xaml    real WinUI 2.3 dictionaries parse cleanly and match their goldens
//   generated limits, markup/x:Bind edge cases and mutation fuzz
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
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

std::string diagnostics(const XamlParseResult& r, const std::string& name)
{
    std::string out;
    for (const auto& d : r.diagnostics)
        out += "\n    " + formatDiagnostic(d, name);
    return out;
}

std::string withCrlf(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
}

void golden(const fs::path& file, const std::string& dumpText)
{
    const auto path = fs::path(file).replace_extension(".golden");
    if (update)
        std::ofstream(path, std::ios::binary) << dumpText;
    check(read(path) == dumpText, file.filename().string() + " matches golden");
}

std::size_t count(const std::string& text, std::string_view needle)
{
    std::size_t n = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
        ++n;
    return n;
}

void valid(const fs::path& dir)
{
    for (const auto& file : files(dir))
    {
        const auto name = file.filename().string();
        const auto source = read(file);
        const auto result = parseXaml(source);
        check(result.ok(), name + " parses" + diagnostics(result, name));
        if (!result.ok())
            continue;
        const auto text = dumpXaml(result.document);
        golden(file, text);
        check(dumpXaml(parseXaml(source).document) == text, name + " reparses identically");
        // Line endings never change positions (columns, lines) or values.
        if (source.find('\r') == std::string::npos)
        {
            const auto crlf = parseXaml(withCrlf(source));
            check(crlf.ok() && dumpXaml(crlf.document) == text,
                  name + " CRLF variant has the same tree and spans");
        }
        std::printf("valid %s: %zu elements, %zu markup extensions\n", name.c_str(), count(text, "element "),
                    count(text, "markup "));
    }
}

void invalid(const fs::path& dir)
{
    for (const auto& file : files(dir))
    {
        const auto name = file.filename().string();
        const auto result = parseXaml(read(file));
        check(!result.ok(), name + " is rejected");
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

void winui(const fs::path& dir)
{
    for (const auto& file : files(dir))
    {
        const auto name = file.filename().string();
        const auto source = read(file);
        const auto result = parseXaml(source);
        check(result.ok(), name + " parses" + diagnostics(result, name));
        if (!result.ok())
            continue;
        const auto text = dumpXaml(result.document);
        golden(file, text);
        check(dumpXaml(parseXaml(source).document) == text, name + " reparses identically");
        std::printf("winui %s: %zu bytes, %zu elements, %zu attributes, %zu markup extensions\n",
                    name.c_str(), source.size(), count(text, "element "), count(text, "attr "),
                    count(text, "markup "));
    }
}

constexpr std::string_view kNamespaces =
    R"(xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml")";

void limits()
{
    const auto nested = [](int depth)
    {
        std::string s = "<Grid " + std::string(kNamespaces) + ">";
        for (int i = 1; i < depth; ++i)
            s += "<Grid>";
        for (int i = 1; i < depth; ++i)
            s += "</Grid>";
        return s + "</Grid>";
    };
    check(parseXaml(nested(128)).ok(), "128 nested elements accepted");
    const auto deep = parseXaml(nested(129));
    check(!deep.ok() && deep.diagnostics.front().message.find("nested deeper than 128") != std::string::npos,
          "129 nested elements rejected");
    check(!parseXaml(nested(100000)).ok(), "100000 nested elements rejected without exhausting the stack");

    std::string markup = "x";
    for (int i = 0; i < 20; ++i)
        markup = "{Binding Converter=" + markup + "}";
    const auto markupResult =
        parseXaml("<TextBlock " + std::string(kNamespaces) + " Text=\"" + markup + "\"/>");
    check(!markupResult.ok() &&
              markupResult.diagnostics.front().message.find("nested deeper than 16") != std::string::npos,
          "markup nesting limit");
    XamlParseOptions small;
    small.maxBytes = 64;
    check(!parseXaml("<Page " + std::string(kNamespaces) + "/>", small).ok(), "size limit");

    // A custom type resolver rejects unknown application types.
    XamlParseOptions strict;
    strict.isKnownType = [](const QName& q)
    { return isBuiltinType(q) && (!q.ns.starts_with("using:") || q.local == "Known"); };
    const std::string app = "<Page " + std::string(kNamespaces) +
                            " xmlns:local=\"using:Demo\"><local:Known/><local:Unknown/></Page>";
    const auto resolved = parseXaml(app, strict);
    check(resolved.diagnostics.size() == 1 &&
              resolved.diagnostics[0].message == "unknown element type 'local:Unknown'",
          "custom type resolver");
}

void spans()
{
    // Columns count code points; positions inside attribute values map through entities.
    const std::string source = "<TextBlock " + std::string(kNamespaces) +
                               "\n  Tag=\"\xF0\x9F\x98\x80\" Text=\"{x:Bind A.&amp;B}\"/>";
    const auto result = parseXaml(source);
    check(!result.ok() && result.diagnostics.front().span.begin.line == 2 &&
              result.diagnostics.front().span.begin.column == 27,
          "x:Bind error inside an entity maps to the source column after an emoji" +
              diagnostics(result, "spans"));
    const auto ok = parseXaml("<TextBlock " + std::string(kNamespaces) +
                              "\n  Tag=\"\xF0\x9F\x98\x80\" Text=\"{x:Bind A.B}\"/>");
    const auto& attribute = ok.document.root->attributes.back();
    const auto& step = attribute.markup->bind->path.steps[1];
    check(ok.ok() && step.name == "B" && step.span.begin.line == 2 && step.span.begin.column == 27 &&
              step.span.end.column == 28,
          "x:Bind step span");
}

// Random edits of every valid input: no crash, no hang, deterministic diagnostics inside the input.
void fuzz(const std::vector<fs::path>& inputs)
{
    std::mt19937 rng(20261013u);
    static const std::string inserts[] = {"<",
                                          ">",
                                          "/>",
                                          "</",
                                          "\"",
                                          "'",
                                          "&",
                                          "&amp;",
                                          "{",
                                          "}",
                                          "{x:Bind ",
                                          ",",
                                          "=",
                                          "<!--",
                                          "-->",
                                          "<![CDATA[",
                                          "]]>",
                                          "<?x?>",
                                          "xmlns:p=\"u\"",
                                          "\xF0\x9F\x98\x80",
                                          "\xC3",
                                          ".",
                                          "["};
    std::size_t total = 0, rejected = 0;
    for (const auto& file : inputs)
    {
        const auto original = read(file);
        for (int i = 0; i < 250; ++i)
        {
            std::string source = original;
            for (int edits = std::uniform_int_distribution<int>(1, 4)(rng); edits > 0 && !source.empty();
                 --edits)
            {
                const auto at = std::uniform_int_distribution<std::size_t>(0, source.size() - 1)(rng);
                if (rng() % 2)
                    source.erase(at, std::uniform_int_distribution<std::size_t>(1, 6)(rng));
                else
                    source.insert(at, inserts[rng() % std::size(inserts)]);
            }
            const auto first = parseXaml(source), second = parseXaml(source);
            ++total;
            if (first.ok())
            {
                check(dumpXaml(first.document) == dumpXaml(second.document),
                      file.filename().string() + " fuzz: deterministic tree");
                continue;
            }
            ++rejected;
            const auto lines = static_cast<velora::u32>(std::ranges::count(source, '\n')) + 1;
            check(first.diagnostics.size() == second.diagnostics.size(),
                  file.filename().string() + " fuzz: deterministic diagnostics");
            for (const auto& d : first.diagnostics)
                check(d.span.begin.line >= 1 && d.span.begin.line <= lines &&
                          d.span.begin.offset <= source.size() && d.span.begin.column >= 1,
                      file.filename().string() + " fuzz: diagnostic inside the input");
        }
    }
    std::printf("fuzz: %zu inputs, %zu rejected\n", total, rejected);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: velora_xaml_test <tests/xaml> [--update-golden]\n");
        return 2;
    }
    const fs::path root = argv[1];
    update = argc > 2 && std::string(argv[2]) == "--update-golden";
    valid(root / "valid");
    invalid(root / "invalid");
    winui(root / "winui");
    limits();
    spans();
    auto inputs = files(root / "valid");
    for (const auto& f : files(root / "winui"))
        inputs.push_back(f);
    fuzz(inputs);
    std::printf("xaml front end: %s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
