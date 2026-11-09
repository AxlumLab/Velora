// velorac: the single Velora build compiler.
//
//   velorac reflect [-DNAME]... [-I<include root>] [--unit NAME] [--include <path as included>]
//                   <header> <output.rkmeta> [output.reflect.cpp]
//       Reflection metadata for an annotated C++ header (depfile <output.rkmeta>.d).
//   velorac xaml [--meta <file.rkmeta>=<header as included>]... [--peer <other.xaml>]... [--app
//   <dictionary.xaml>]...
//                <input.xaml> <output dir>
//       C++ view for a XAML file: <stem>.g.hpp, <stem>.g.cpp (depfile <stem>.g.d).
//   velorac resources [--peer <other.xaml>]... [--app <dictionary.xaml>]... <dictionary.xaml> <stamp>
//       Checks a ResourceDictionary file in both themes; writes <stamp> when it is valid.
//   velorac pri --name N [--language L] <resource root> <output.vpri>
//       Resource index for a tree: files by qualifier naming, Strings/<lang>/*.resw as strings.
//   velorac pack --name N [--version V] [--language L] [--compress] <resource root> <output.vpak>
//       The same index plus every file it names, in one package.
//   velorac dump-pri [--lang L] [--scale S] [--theme light|dark|high-contrast] <file.vpri|file.vpak>
//       Every candidate; with a context, the one that wins there (candidate tracing).
//   velorac verify-pack <file.vpak>   check every payload of a package
//   velorac check <file.xaml>     validate a XAML file
//   velorac ast <file.xaml>       print its syntax tree with source spans
//
// Outputs are rewritten only when their content changes, so dependents do not rebuild.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <velora/reflect/Metagen.hpp>
#include <velora/resources/PackageFile.hpp>
#include <velora/resources/ResourceIndex.hpp>
#include <velora/xaml/CodeBehindWriter.hpp>
#include <velora/xaml/ReswReader.hpp>
#include <velora/xaml/XamlBinary.hpp>
#include <velora/xaml/XamlIRBuilder.hpp>
#include <velora/xaml/Compiler.hpp>
#include <velora/xaml/Parser.hpp>

namespace fs = std::filesystem;

namespace
{

std::optional<std::string> read(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(in), {}};
}

bool writeIfChanged(const fs::path& path, const std::string& text)
{
    if (const auto existing = read(path); existing && *existing == text)
        return true;
    std::ofstream out(path, std::ios::binary);
    out << text;
    return static_cast<bool>(out);
}

// Makefile-style depfile path: forward slashes, spaces and '#' escaped.
std::string depfilePath(const fs::path& path)
{
    std::string out;
    for (char c : fs::absolute(path).lexically_normal().generic_string())
    {
        if (c == ' ' || c == '#')
            out += '\\';
        out += c;
    }
    return out;
}

void writeDepfile(const fs::path& depfile, const fs::path& target, const fs::path& input,
                  const std::vector<fs::path>& dependencies)
{
    std::string text = depfilePath(target) + ": " + depfilePath(input);
    for (const auto& d : dependencies)
        text += " " + depfilePath(d);
    std::ofstream(depfile, std::ios::binary) << text << '\n';
}

int usage()
{
    std::cerr
        << "usage:\n"
           "  velorac reflect [-DNAME]... [-Iroot] [--unit NAME] [--include path] <header> <output.rkmeta> "
           "[output.reflect.cpp]\n"
           "  velorac xaml [--meta file.rkmeta=include]... [--peer other.xaml]... [--app dictionary.xaml]... "
           "<input.xaml> <output dir>\n"
           "  velorac resources [--peer other.xaml]... [--app dictionary.xaml]... <dictionary.xaml> <stamp>\n"
           "  velorac check <file.xaml>\n"
           "  velorac ast <file.xaml>\n"
           "  velorac vxbf [--include header]... [--meta file.rkmeta]... <input.xaml> <base dir> <output "
           "dir>\n"
           "  velorac dump-xbf <file.vxbf>\n"
           "  velorac verify-xbf <file.vxbf>\n"
           "  velorac pri --name N [--language L] <resource root> <output.vpri>\n"
           "  velorac pack --name N [--version V] [--language L] [--compress] <resource root> <output.vpak>\n"
           "  velorac dump-pri [--lang L] [--scale S] [--theme light|dark|high-contrast] "
           "<file.vpri|file.vpak>\n"
           "  velorac verify-pack <file.vpak>\n"
           "  velorac embed-pack <file.vpak> <output.cpp> <app|framework>\n"
           "  velorac external-pack <name.vpak> <output.cpp> <app|framework>\n";
    return 2;
}

int reflect(const std::vector<std::string>& args)
{
    velora::RkParseOptions options;
    std::string unit, include;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];
        if (arg.starts_with("-D") && arg.size() > 2)
            options.defines.emplace_back(arg.substr(2));
        else if (arg.starts_with("-I") && arg.size() > 2)
            options.includeRoot = fs::path(arg.substr(2));
        else if (arg == "--unit" && i + 1 < args.size())
            unit = args[++i];
        else if (arg == "--include" && i + 1 < args.size())
            include = args[++i];
        else
            positional.emplace_back(arg);
    }
    if (positional.size() != 2 && positional.size() != 3)
        return usage();
    const fs::path header = positional[0], output = positional[1];
    const auto source = read(header);
    if (!source)
    {
        std::cerr << header.string() << ": cannot open input\n";
        return 3;
    }
    const auto generated = velora::generateFromSource(*source, header.string(), options);
    if (!generated.result.ok())
    {
        std::cerr << generated.result.error << '\n';
        return 1;
    }
    if (!writeIfChanged(output, generated.result.text))
    {
        std::cerr << output.string() << ": cannot write\n";
        return 4;
    }
    if (positional.size() == 3)
    {
        if (unit.empty())
            unit = header.stem().string();
        if (!writeIfChanged(positional[2],
                            velora::generateReflectCpp(generated.result.document, unit, include)))
        {
            std::cerr << positional[2] << ": cannot write\n";
            return 4;
        }
    }
    writeDepfile(output.string() + ".d", output, header, generated.dependencies);
    return 0;
}

int xaml(const std::vector<std::string>& args)
{
    velora::CompileOptions options;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& arg = args[i];
        if (arg == "--meta" && i + 1 < args.size())
        {
            const std::string& value = args[++i];
            const auto eq = value.rfind('=');
            if (eq == std::string::npos)
                return usage();
            options.metadata.emplace_back(value.substr(0, eq), value.substr(eq + 1));
        }
        else if (arg == "--peer" && i + 1 < args.size())
        {
            options.peers.emplace_back(args[++i]);
        }
        else if (arg == "--app" && i + 1 < args.size())
        {
            options.application.emplace_back(args[++i]);
        }
        else
        {
            positional.push_back(arg);
        }
    }
    if (positional.size() != 2)
        return usage();
    const fs::path input = positional[0], outDir = positional[1];
    const auto source = read(input);
    if (!source)
    {
        std::cerr << input.string() << ": cannot open input\n";
        return 3;
    }
    const auto result = velora::compile(*source, input, options);
    for (const auto& d : result.diagnostics)
        std::cerr << velora::formatDiagnostic(d, input.string()) << '\n';
    if (!result.ok())
        return 1;
    fs::create_directories(outDir);
    const auto stem = input.stem().string();
    if (!writeIfChanged(outDir / result.headerName, result.header) ||
        !writeIfChanged(outDir / (stem + ".g.cpp"), result.source))
    {
        std::cerr << "velorac: cannot write outputs in " << outDir.string() << '\n';
        return 4;
    }
    // The depfile names the first output of the build rule (the header).
    writeDepfile(outDir / (stem + ".g.d"), outDir / result.headerName, input, result.dependencies);
    return 0;
}

int resources(const std::vector<std::string>& args)
{
    velora::CompileOptions options;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--peer" && i + 1 < args.size())
            options.peers.emplace_back(args[++i]);
        else if (args[i] == "--app" && i + 1 < args.size())
            options.application.emplace_back(args[++i]);
        else
            positional.push_back(args[i]);
    }
    if (positional.size() != 2)
        return usage();
    const fs::path input = positional[0], stamp = positional[1];
    const auto source = read(input);
    if (!source)
    {
        std::cerr << input.string() << ": cannot open input\n";
        return 3;
    }
    const auto result = velora::checkResources(*source, input, options);
    for (const auto& d : result.diagnostics)
        std::cerr << velora::formatDiagnostic(d, input.string()) << '\n';
    if (!result.ok())
        return 1;
    writeDepfile(stamp.string() + ".d", stamp, input, result.dependencies);
    return writeIfChanged(stamp, "ok\n") ? 0 : 4;
}

int inspect(const std::vector<std::string>& args, bool printTree)
{
    if (args.size() != 1)
        return usage();
    const auto source = read(args[0]);
    if (!source)
    {
        std::cerr << args[0] << ": cannot open input\n";
        return 3;
    }
    const auto result = velora::parseXaml(*source);
    for (const auto& d : result.diagnostics)
        std::cerr << velora::formatDiagnostic(d, args[0]) << '\n';
    if (!result.ok())
        return 1;
    if (printTree)
        std::cout << velora::dumpXaml(result.document);
    return 0;
}

} // namespace

// .vxbf pipeline (R06): XAML -> XamlIR -> .vxbf + code-behind. Outputs keep the input's path
// relative to the base dir, so Views/A/Main.xaml and Views/B/Main.xaml never collide.
int vxbf(const std::vector<std::string>& args)
{
    std::vector<std::string> includes, positional;
    velora::MetadataCatalog catalog;
    bool haveMetadata = false;
    bool embedVxbf = true;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--no-embed")
            embedVxbf = false;
        else if (args[i] == "--include" && i + 1 < args.size())
            includes.push_back(args[++i]);
        else if (args[i] == "--meta" && i + 1 < args.size())
        {
            const fs::path file = args[++i];
            const auto text = read(file);
            const auto metadata = text ? velora::parseMetadata(*text) : velora::MetadataResult{};
            if (!metadata.ok())
            {
                std::cerr << file.string() << ": " << (text ? metadata.error : "cannot open input") << '\n';
                return 3;
            }
            catalog.add(metadata.document);
            haveMetadata = true;
        }
        else
            positional.push_back(args[i]);
    }
    if (positional.size() != 3)
        return usage();
    const fs::path input = positional[0], base = positional[1], outDir = positional[2];
    const auto source = read(input);
    if (!source)
    {
        std::cerr << input.string() << ": cannot open input\n";
        return 3;
    }
    const fs::path relative =
        fs::absolute(input).lexically_normal().lexically_relative(fs::absolute(base).lexically_normal());
    if (relative.empty() || relative.native().starts_with(fs::path("..").native()))
    {
        std::cerr << input.string() << ": not under the base dir " << base.string() << '\n';
        return 2;
    }
    const auto parsed = velora::parseXaml(*source);
    for (const auto& d : parsed.diagnostics)
        std::cerr << velora::formatDiagnostic(d, input.string()) << '\n';
    if (!parsed.ok())
        return 1;
    auto built =
        velora::buildXamlIR(parsed.document, relative.generic_string(), haveMetadata ? &catalog : nullptr);
    for (const auto& d : built.diagnostics)
        std::cerr << velora::formatDiagnostic(d, input.string()) << '\n';
    if (!built.ok())
        return 1;
    const bool dictionary = built.ir.instructions.front().op == velora::XamlIROp::kBeginResources;
    if (built.ir.className.empty() && !dictionary)
    {
        std::cerr
            << input.string()
            << ": the .vxbf pipeline compiles pages with an x:Class root and ResourceDictionary files\n";
        return 1;
    }
    const auto bytes = velora::writeVxbf(built.ir);
    const auto code = velora::writeCodeBehind(built.ir, bytes, relative.generic_string(), includes,
                                              built.xbinds, built.itemHandlers, embedVxbf);
    const fs::path stem = outDir / relative.parent_path() / relative.stem();
    fs::create_directories(stem.parent_path());
    const bool written =
        writeIfChanged(fs::path(stem).replace_extension(".vxbf"), std::string(bytes.begin(), bytes.end())) &&
        writeIfChanged(stem.string() + ".g.hpp", code.header) &&
        writeIfChanged(stem.string() + ".g.cpp", code.source);
    if (!written)
    {
        std::cerr << stem.string() << ": cannot write outputs\n";
        return 4;
    }
    return 0;
}

// dump-xbf prints the IR; verify-xbf only checks the file (structure, version, checksum).
int readXbf(const std::vector<std::string>& args, bool print)
{
    if (args.size() != 1)
        return usage();
    const auto bytes = read(args[0]);
    if (!bytes)
    {
        std::cerr << args[0] << ": cannot open input\n";
        return 3;
    }
    const auto result =
        velora::readVxbf(std::span(reinterpret_cast<const velora::u8*>(bytes->data()), bytes->size()));
    if (const auto* error = std::get_if<velora::VxbfError>(&result))
    {
        std::cerr << args[0] << ": " << error->message << '\n';
        return 1;
    }
    if (print)
        std::cout << velora::dump(std::get<velora::XamlIR>(result));
    else
        std::cout << args[0] << ": ok\n";
    return 0;
}

// ---- resources (R08.6) -------------------------------------------------------------------------

struct PackageArgs
{
    std::string name, version = "1.0.0", language = "en-US";
    bool compress = false;
    std::vector<std::string> positional;
};

std::optional<PackageArgs> packageArgs(const std::vector<std::string>& args)
{
    PackageArgs out;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--name" && i + 1 < args.size())
            out.name = args[++i];
        else if (args[i] == "--version" && i + 1 < args.size())
            out.version = args[++i];
        else if (args[i] == "--language" && i + 1 < args.size())
            out.language = args[++i];
        else if (args[i] == "--compress")
            out.compress = true;
        else if (args[i].starts_with("--"))
            return std::nullopt;
        else
            out.positional.push_back(args[i]);
    }
    if (out.name.empty() || out.positional.size() != 2)
        return std::nullopt;
    return out;
}

bool writeBytes(const fs::path& path, const std::vector<velora::u8>& bytes)
{
    if (path.has_parent_path())
        fs::create_directories(path.parent_path());
    return writeIfChanged(path, std::string(bytes.begin(), bytes.end()));
}

// pri: the index only, for loose resources (LooseFileProvider) or inspection.
int pri(const std::vector<std::string>& args)
{
    const auto options = packageArgs(args);
    if (!options || options->compress)
        return usage();
    auto index = velora::buildResourceIndex(options->positional[0], options->name, options->language);
    if (const auto* error = std::get_if<std::string>(&index))
    {
        std::cerr << options->positional[0] << ": " << *error << '\n';
        return 1;
    }
    if (!writeBytes(options->positional[1], velora::writeVpri(std::get<velora::ResourceIndex>(index))))
    {
        std::cerr << options->positional[1] << ": cannot write output\n";
        return 4;
    }
    return 0;
}

// pack: the same index plus every file it names. Loose and packed resolve identically because
// both use this index.
int pack(const std::vector<std::string>& args)
{
    const auto options = packageArgs(args);
    if (!options)
        return usage();
    const fs::path root = options->positional[0];
    auto built = velora::buildResourceIndex(root, options->name, options->language);
    if (const auto* error = std::get_if<std::string>(&built))
    {
        std::cerr << root.string() << ": " << *error << '\n';
        return 1;
    }
    auto& index = std::get<velora::ResourceIndex>(built);
    velora::PackageWriter writer({options->name, options->version, options->language});
    std::vector<std::string> files;
    for (const auto& [logical, variants] : index.candidates)
        for (const auto& candidate : variants)
            if (candidate.kind == velora::CandidateKind::kFile)
                files.push_back(candidate.path);
    std::ranges::sort(files);
    for (const auto& file : files)
    {
        const auto bytes = read(root / file);
        if (!bytes)
        {
            std::cerr << (root / file).string() << ": cannot read\n";
            return 3;
        }
        writer.add(file, std::span(reinterpret_cast<const velora::u8*>(bytes->data()), bytes->size()),
                   options->compress);
    }
    writer.setIndex(std::move(index));
    const auto finished = writer.finish();
    if (const auto* error = std::get_if<velora::VpakError>(&finished))
    {
        std::cerr << root.string() << ": " << error->message << '\n';
        return 1;
    }
    if (!writeBytes(options->positional[1], std::get<std::vector<velora::u8>>(finished)))
    {
        std::cerr << options->positional[1] << ": cannot write output\n";
        return 4;
    }
    return 0;
}

int dumpPri(const std::vector<std::string>& args)
{
    velora::ResourceContext context;
    bool traced = false;
    std::vector<std::string> positional;
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--lang" && i + 1 < args.size())
            context.language = args[++i], traced = true;
        else if (args[i] == "--scale" && i + 1 < args.size())
            context.scale = std::stof(args[++i]), traced = true;
        else if (args[i] == "--theme" && i + 1 < args.size())
        {
            const std::string theme = args[++i];
            if (theme == "light")
                context.theme = velora::ResourceTheme::kLight;
            else if (theme == "dark")
                context.theme = velora::ResourceTheme::kDark;
            else if (theme == "high-contrast")
                context.theme = velora::ResourceTheme::kHighContrast;
            else
                return usage();
            traced = true;
        }
        else
            positional.push_back(args[i]);
    }
    if (positional.size() != 1)
        return usage();
    const fs::path input = positional[0];
    if (input.extension() == ".vpak")
    {
        auto opened = velora::PackageFile::open(input);
        if (const auto* error = std::get_if<velora::VpakError>(&opened))
        {
            std::cerr << error->message << '\n';
            return 1;
        }
        const auto& package = *std::get<std::shared_ptr<velora::PackageFile>>(opened);
        std::cout << package.dump() << velora::dumpVpri(package.index(), traced ? &context : nullptr);
        return 0;
    }
    const auto bytes = read(input);
    if (!bytes)
    {
        std::cerr << input.string() << ": cannot open input\n";
        return 3;
    }
    const auto index =
        velora::readVpri(std::span(reinterpret_cast<const velora::u8*>(bytes->data()), bytes->size()));
    if (const auto* error = std::get_if<velora::VpriError>(&index))
    {
        std::cerr << input.string() << ": " << error->message << '\n';
        return 1;
    }
    std::cout << velora::dumpVpri(std::get<velora::ResourceIndex>(index), traced ? &context : nullptr);
    return 0;
}

int verifyPack(const std::vector<std::string>& args)
{
    if (args.size() != 1)
        return usage();
    auto opened = velora::PackageFile::open(args[0]);
    if (const auto* error = std::get_if<velora::VpakError>(&opened))
    {
        std::cerr << error->message << '\n';
        return 1;
    }
    const auto problems = std::get<std::shared_ptr<velora::PackageFile>>(opened)->verify();
    for (const auto& problem : problems)
        std::cerr << args[0] << ": " << problem << '\n';
    if (!problems.empty())
        return 1;
    std::cout << args[0] << ": ok\n";
    return 0;
}

// Convert a finished VPAK into one embedded resource provider (not one C++ array per XAML page).
int embedPackage(const std::vector<std::string>& args)
{
    if (args.size() != 3 || (args[2] != "app" && args[2] != "framework"))
        return usage();
    const auto bytes = read(args[0]);
    if (!bytes)
        return 3;
    const std::string ns = args[2] == "app" ? "kApplication" : "kFramework";
    std::ostringstream source;
    source << "// Generated by velorac embed-pack; do not edit.\n"
           << "#include <velora/resources/ResourceProvider.hpp>\n\n"
           << "namespace\n{\nconst velora::u8 kPackage[] = {";
    for (std::size_t i = 0; i < bytes->size(); ++i)
        source << (i % 20 == 0 ? "\n    " : " ") << static_cast<unsigned char>((*bytes)[i]) << ',';
    source << "\n};\nconst bool kMounted = []\n{\n"
           << "    const auto error = velora::ResourceManager::instance().mountEmbeddedPackage("
           << "velora::ResourceNamespace::" << ns << ", kPackage);\n"
           << "    return error.empty();\n}();\n} // namespace\n";
    return writeIfChanged(args[1], source.str()) ? 0 : 4;
}

int externalPackage(const std::vector<std::string>& args)
{
    if (args.size() != 3 || (args[2] != "app" && args[2] != "framework"))
        return usage();
    const std::string& name = args[0];
    if (name.empty() || name == "." || name == ".." ||
        !std::ranges::all_of(name, [](unsigned char c) {
            return std::isalnum(c) || c == '.' || c == '_' || c == '-';
        }))
        return usage();
    std::ostringstream source;
    source << "// Generated by velorac external-pack; do not edit.\n"
           << "#include <velora/resources/ResourceProvider.hpp>\n\n"
           << "namespace\n{\nconst bool kMounted = []\n{\n"
           << "    const auto error = velora::ResourceManager::instance().mountExternalPackage("
           << "velora::ResourceNamespace::"
           << (args[2] == "app" ? "kApplication" : "kFramework") << ", \"" << args[0]
           << "\");\n    return error.empty();\n}();\n} // namespace\n";
    return writeIfChanged(args[1], source.str()) ? 0 : 4;
}

int main(int argc, char** argv)
{
    if (argc < 2)
        return usage();
    const std::string command = argv[1];
    const std::vector<std::string> args(argv + 2, argv + argc);
    if (command == "reflect")
        return reflect(args);
    if (command == "xaml")
        return xaml(args);
    if (command == "resources")
        return resources(args);
    if (command == "check")
        return inspect(args, false);
    if (command == "vxbf")
        return vxbf(args);
    if (command == "dump-xbf")
        return readXbf(args, true);
    if (command == "verify-xbf")
        return readXbf(args, false);
    if (command == "ast")
        return inspect(args, true);
    if (command == "pri")
        return pri(args);
    if (command == "pack")
        return pack(args);
    if (command == "dump-pri")
        return dumpPri(args);
    if (command == "verify-pack")
        return verifyPack(args);
    if (command == "embed-pack")
        return embedPackage(args);
    if (command == "external-pack")
        return externalPackage(args);
    return usage();
}
