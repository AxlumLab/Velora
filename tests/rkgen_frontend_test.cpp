// Reflection frontend (ANTLR) test.
//
//   1. tests/rkgen/valid/*.hpp and the project-owned reference fixtures: parsing succeeds, normalize()
//      equals the checked-in golden, and the CRLF variant normalizes and generates .rkmeta
//      identically.
//   2. tests/rkgen/invalid/*.hpp: errors are reported at the lines named by "// expect-error:".
//   3. Generated samples: every generated type is reflected, CRLF-invariant as in 1.
//   4. Mutation fuzz: no crash or hang, and every error has a line inside the input.
//
// usage: veloraRkgenFrontendTest <tests/rkgen> <reference testdata dir> [--update-golden]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <velora/reflect/Compiler.hpp>
#include <velora/reflect/Metagen.hpp>

namespace fs = std::filesystem;
using namespace velora;

namespace
{

int failures = 0;
bool updateGolden = false;

void fail(const std::string& message)
{
    std::fprintf(stderr, "FAIL %s\n", message.c_str());
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

void write(const fs::path& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary) << text;
}

std::vector<fs::path> headers(const fs::path& dir)
{
    std::vector<fs::path> out;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().extension() == ".hpp")
            out.push_back(entry.path());
    std::ranges::sort(out);
    return out;
}

// "// rkgen: -DA -DB" on the first line selects the macro context.
RkParseOptions optionsFor(const std::string& source, const fs::path& includeRoot)
{
    RkParseOptions options;
    options.includeRoot = includeRoot;
    const auto first = source.substr(0, source.find('\n'));
    if (const auto at = first.find("// rkgen:"); at != std::string::npos)
    {
        std::istringstream args(first.substr(at + 9));
        for (std::string arg; args >> arg;)
            if (arg.starts_with("-D"))
                options.defines.push_back(arg.substr(2));
    }
    return options;
}

std::string crlf(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
}

std::string describe(const RkParseResult& r)
{
    std::string out;
    for (const auto& d : r.diagnostics)
        out += "\n    " + std::to_string(d.line) + ":" + std::to_string(d.column) + " " + d.message;
    return out;
}

std::string rkmeta(const RkAst& ast)
{
    return serializeMetadata(generate(ast));
}

// Parses the source and its CRLF variant; returns the normalized form, or empty after
// reporting a failure.
std::string checkValid(const std::string& name, const std::string& source, const RkParseOptions& options,
                       std::size_t* typeCount = nullptr)
{
    const auto lf = parseReflection(source, options);
    if (!lf.ok())
    {
        fail(name + ": rejected" + describe(lf));
        return {};
    }
    const auto cr = parseReflection(crlf(source), options);
    if (!cr.ok())
    {
        fail(name + " (CRLF): rejected" + describe(cr));
        return {};
    }
    const auto normalized = normalizeReflection(lf.ast);
    if (normalizeReflection(cr.ast) != normalized || rkmeta(cr.ast) != rkmeta(lf.ast))
    {
        fail(name + ": CRLF variant differs");
        return {};
    }
    if (typeCount)
        *typeCount = lf.ast.types.size();
    return normalized;
}

void checkGolden(const fs::path& header, const std::string& actual)
{
    const auto golden = fs::path(header).replace_extension(".golden");
    if (updateGolden)
    {
        write(golden, actual);
        return;
    }
    if (!fs::exists(golden))
        return fail(header.filename().string() + ": missing golden");
    if (read(golden) != actual)
        fail(header.filename().string() + ": golden mismatch\n--- golden\n" + read(golden) + "--- actual\n" +
             actual);
}

void validCorpus(const fs::path& dir)
{
    for (const auto& path : headers(dir))
    {
        const auto source = read(path);
        const auto normalized = checkValid(path.filename().string(), source, optionsFor(source, dir));
        if (!normalized.empty())
            checkGolden(path, normalized);
    }
}

void invalidCorpus(const fs::path& dir)
{
    for (const auto& path : headers(dir))
    {
        const auto source = read(path);
        const auto name = path.filename().string();
        std::istringstream header(source.substr(0, source.find('\n')));
        std::string comment, tag;
        header >> comment >> tag;
        std::vector<std::size_t> lines;
        for (std::string word; header >> word;)
            lines.push_back(std::stoul(word));
        const auto result = parseReflection(source, optionsFor(source, dir));
        std::vector<std::size_t> errors;
        for (const auto& d : result.diagnostics)
            if (d.severity == RkDiagnostic::Severity::kError)
                errors.push_back(d.line);
        if (result.ok())
            fail(name + ": accepted invalid input");
        for (std::size_t line : lines)
            if (std::ranges::find(errors, line) == errors.end())
                fail(name + ": no error on line " + std::to_string(line) + describe(result));
        std::printf("invalid %s: %zu error(s)%s\n", name.c_str(), errors.size(), describe(result).c_str());
    }
}

void referenceFixtures(const fs::path& reference, const fs::path& goldens)
{
    const auto inputs = headers(reference);
    for (const char* name : {"BasicTypes.hpp", "Nested.hpp", "Player.hpp"})
        if (std::ranges::none_of(inputs, [&](const auto& path) { return path.filename() == name; }))
            fail("reference corpus is missing " + std::string(name));
    for (const auto& path : inputs)
    {
        const auto source = read(path);
        const auto normalized =
            checkValid("reference/" + path.filename().string(), source, optionsFor(source, reference));
        if (normalized.empty())
            fail("reference/" + path.filename().string() + ": no reflected types");
        else
            checkGolden(goldens / path.filename(), normalized);
    }
}

// Random valid declarations; types() counts the types emitted by the last sample.
class Generator
{
public:
    explicit Generator(unsigned seed) : m_rng(seed) {}

    std::string sample()
    {
        m_types = 0;
        std::ostringstream out;
        const int namespaces = pick(1, 3);
        for (int n = 0; n < namespaces; ++n)
        {
            out << "namespace " << ident("ns") << (chance(3) ? "::" + ident("sub") : "") << " {\n";
            const int types = pick(1, 4);
            for (int t = 0; t < types; ++t)
                type(out);
            out << "}\n";
            if (chance(3))
                out << "// " << ident("note") << "\n\n";
        }
        return out.str();
    }

    std::size_t types() const noexcept
    {
        return m_types;
    }

private:
    int pick(int lo, int hi)
    {
        return std::uniform_int_distribution<int>(lo, hi)(m_rng);
    }
    bool chance(int oneIn)
    {
        return pick(1, oneIn) == 1;
    }
    std::string ident(const char* stem)
    {
        return std::string(stem) + std::to_string(pick(0, 999));
    }
    std::string ws()
    {
        static const char* spaces[] = {" ", "  ", "\t", " /* c */ "};
        return spaces[pick(0, 3)];
    }

    std::string typeName()
    {
        static const char* simple[] = {"int",         "float", "double",        "bool", "std::string",
                                       "const char*", "Vec3",  "std::uint32_t", "void*"};
        switch (pick(0, 5))
        {
        case 0:
            return "std::vector<" + std::string(simple[pick(0, 8)]) + ">";
        case 1:
            return "std::map<int, std::vector<" + std::string(simple[pick(0, 8)]) + ">>";
        case 2:
            return "const " + std::string(simple[pick(0, 6)]) + "&";
        default:
            return simple[pick(0, 8)];
        }
    }

    void type(std::ostringstream& out)
    {
        ++m_types;
        if (chance(5))
        {
            out << (chance(2) ? "cenum class " : "rkenum ") << ident("E")
                << (chance(2) ? " : std::uint8_t" : "") << "\n{\n";
            const int values = pick(1, 6);
            for (int v = 0; v < values; ++v)
                out << "    " << ident("V") << (chance(3) ? " = " + std::to_string(pick(0, 50)) : "")
                    << ",\n";
            out << "};\n";
            return;
        }
        const bool isClass = chance(2);
        if (chance(4))
            out << "rkattr(" << ident("Tag") << ")\n";
        const auto name = ident(isClass ? "C" : "S");
        out << (isClass ? (chance(2) ? "cclass " : "rkclass ") : (chance(2) ? "cstruct " : "rkstruct "))
            << name;
        if (chance(3))
            out << " : public " << ident("Base") << (chance(2) ? "<int>" : "");
        out << "\n{\n";
        if (isClass)
            out << "public:\n";
        std::vector<std::string> used;
        const int members = pick(0, 7);
        for (int m = 0; m < members; ++m)
        {
            auto field = ident("f");
            if (std::ranges::find(used, field) != used.end())
                continue;
            used.push_back(field);
            switch (pick(0, 4))
            {
            case 0:
                out << "    cfield(" << (chance(2) ? "Get" + field : "") << (chance(3) ? ", Set" + field : "")
                    << ")" << ws() << typeName() << ' ' << field << ";\n";
                break;
            case 1:
                out << "    cfunc" << (chance(4) ? " static" : "") << ' ' << typeName() << ' ' << ident("Do")
                    << '(';
                for (int p = 0, n = pick(0, 3); p < n; ++p)
                    out << (p ? ", " : "") << typeName() << ' ' << ident("a");
                out << ')' << (chance(2) ? " const" : "") << (chance(3) ? " noexcept" : "") << ";\n";
                break;
            case 2:
                if (isClass)
                    out << "    rkctor " << name << "(int " << ident("v") << ");\n";
                else
                    out << "    " << typeName() << ' ' << field << (chance(3) ? " = {}" : "") << ";\n";
                break;
            case 3:
                out << "    int " << ident("unreflected") << "(int x) const;\n";
                break;
            default:
                if (isClass)
                    out << "private:\n    int " << field << ";\npublic:\n";
                else
                    out << "    static int " << field << ";\n";
                break;
            }
        }
        out << "};\n";
    }

    std::mt19937 m_rng;
    std::size_t m_types = 0;
};

void generatedFuzz(int count)
{
    Generator generator(20261013u);
    for (int i = 0; i < count; ++i)
    {
        const auto source = generator.sample();
        const auto name = "generated #" + std::to_string(i);
        std::size_t types = 0;
        const bool valid = !checkValid(name, source, {}, &types).empty();
        if (valid && types != generator.types())
            fail(name + ": reflected " + std::to_string(types) + " of " + std::to_string(generator.types()) +
                 " types");
        if (!valid || types != generator.types())
        {
            std::fprintf(stderr, "%s\n", source.c_str());
            return;
        }
    }
}

void mutationFuzz(const fs::path& dir, int perFile)
{
    std::mt19937 rng(7u);
    static const std::string inserts[] = {"{",        "}",        ";",     "(",  ")", "<", ">", "#if X\n",
                                          "#endif\n", "cfield()", "cfunc", "::", ",", "=", "\n"};
    std::size_t rejected = 0, total = 0;
    for (const auto& path : headers(dir))
    {
        const auto original = read(path);
        for (int i = 0; i < perFile; ++i)
        {
            std::string source = original;
            for (int edits = std::uniform_int_distribution<int>(1, 4)(rng); edits > 0 && !source.empty();
                 --edits)
            {
                const auto at = std::uniform_int_distribution<std::size_t>(0, source.size() - 1)(rng);
                if (rng() % 2)
                    source.erase(at, std::uniform_int_distribution<std::size_t>(1, 8)(rng));
                else
                    source.insert(at, inserts[rng() % std::size(inserts)]);
            }
            const auto lines = static_cast<std::size_t>(std::ranges::count(source, '\n')) + 1;
            const auto result = parseReflection(source, {});
            ++total;
            if (result.ok())
                continue;
            ++rejected;
            for (const auto& d : result.diagnostics)
                if (d.line < 1 || d.line > lines)
                    fail(path.filename().string() +
                         ": diagnostic line out of range: " + std::to_string(d.line));
        }
    }
    std::printf("mutation fuzz: %zu inputs, rejected %zu\n", total, rejected);
}

} // namespace

int runTests(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(
            stderr,
            "usage: velora_rkgen_frontend_test <tests/rkgen> <reference testdata> [--update-golden]\n");
        return 2;
    }
    const fs::path corpus = argv[1], reference = argv[2];
    updateGolden = argc > 3 && std::string(argv[3]) == "--update-golden";

    validCorpus(corpus / "valid");
    referenceFixtures(reference, corpus / "reference");
    invalidCorpus(corpus / "invalid");
    generatedFuzz(500);
    mutationFuzz(corpus / "valid", 200);

    std::printf("rkgen frontend: %s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
    try
    {
        return runTests(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "rkgen frontend: %s\n", error.what());
        return 1;
    }
}
