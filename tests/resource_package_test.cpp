// R08 acceptance: qualifier ranking (language, contrast, theme, scale), the .vpri index and the
// .vpak container (round trip, corruption, on-demand reads), raw deflate, loose/packed equivalence,
// .resw strings per language, different contexts per root, package override and isolation.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <velora/resources/Deflate.hpp>
#include <velora/resources/PackageFile.hpp>
#include <velora/resources/ResourceIndex.hpp>
#include <velora/resources/ResourceProvider.hpp>
#include <velora/resources/ResourceScan.hpp>

using namespace velora;
namespace fs = std::filesystem;

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

std::vector<u8> bytesOf(std::string_view text)
{
    return {text.begin(), text.end()};
}

ResourceContext context(std::string language, f32 scale = 1.0f, ResourceTheme theme = ResourceTheme::kLight)
{
    ResourceContext c;
    c.language = std::move(language);
    c.scale = scale;
    c.theme = theme;
    return c;
}

ResourceQualifier q(std::string_view text)
{
    return *parseQualifiers(text);
}

// A scratch directory under the system temp directory, removed at the end.
struct TempDir
{
    fs::path path;
    TempDir()
    {
        std::random_device random;
        path = fs::temp_directory_path() / ("velora-r08-" + std::to_string(random()));
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }
    void write(const std::string& relative, std::string_view text) const
    {
        const auto file = path / relative;
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    }
};

void qualifiers()
{
    for (const char* text : {"", "lang-en-US", "scale-150", "theme-dark", "contrast-high",
                             "lang-zh-Hans-CN_contrast-high_scale-200", "lang-en_theme-light_scale-100"})
    {
        const auto parsed = parseQualifiers(text);
        check(parsed && qualifierString(*parsed) == text, "a qualifier string round-trips");
    }
    check(parseQualifiers("language-en-US") == parseQualifiers("lang-en-US"),
          "language- is an alias of lang-");
    for (const char* bad :
         {"scale-abc", "theme-blue", "lang-", "size-10", "scale-150__theme-dark", "scale-150_"})
        check(!parseQualifiers(bad), "a malformed qualifier is rejected");

    check(languageMatchScore("en-US", "en-US") == 2u && languageMatchScore("en", "en-US") == 1u &&
              languageMatchScore("und", "en-US") == 0u,
          "language: exact > parent > neutral");
    check(!languageMatchScore("de-DE", "zh-CN") && !languageMatchScore("en-US", "en") &&
              !languageMatchScore("zh-Hans", "zh-Hant-TW"),
          "language: another tag, a longer tag or another script cannot serve");
    check(languageMatchScore("ZH-cn", "zh-CN") == 2u, "language tags compare case-insensitively");
    check(scaleOf(1.0f) == 100 && scaleOf(1.5f) == 150 && scaleOf(2.0f) == 200 && scaleOf(0.0f) == 100,
          "rasterization scale maps to the 100-based qualifier");
}

void ranking()
{
    ResourceIndex index;
    index.name = "Test";
    index.defaultLanguage = "en-US";
    for (const char* scale : {"scale-100", "scale-150", "scale-200"})
        index.addFile("Assets/Logo.png", std::string("Assets/Logo.") + scale + ".png", q(scale));
    const auto at = [&](f32 scale)
    {
        const auto* c = index.resolve("Assets/Logo.png", context("en-US", scale));
        return c ? c->qualifier.scale : 0u;
    };
    check(at(1.0f) == 100 && at(1.5f) == 150 && at(2.0f) == 200, "scale: the exact candidate wins");
    check(at(1.25f) == 150 && at(1.75f) == 200, "scale: else the closest larger candidate");
    check(at(3.0f) == 200, "scale: else the closest smaller candidate");

    index.addFile("Assets/Icon.png", "Assets/Icon.png");
    index.addFile("Assets/Icon.png", "Assets/Icon.theme-dark.png", q("theme-dark"));
    index.addFile("Assets/Icon.png", "Assets/Icon.contrast-high.png", q("contrast-high"));
    const auto icon = [&](ResourceTheme theme)
    {
        const auto* c = index.resolve("Assets/Icon.png", context("en-US", 1.0f, theme));
        return c ? c->path : std::string();
    };
    check(icon(ResourceTheme::kLight) == "Assets/Icon.png", "theme: a neutral candidate serves light");
    check(icon(ResourceTheme::kDark) == "Assets/Icon.theme-dark.png",
          "theme: the dark candidate wins in dark");
    check(icon(ResourceTheme::kHighContrast) == "Assets/Icon.contrast-high.png",
          "contrast: the high-contrast candidate serves high contrast");
    ResourceIndex onlyLight = index;
    onlyLight.candidates["Assets/Icon.png"] = {
        {CandidateKind::kFile, "Assets/Icon.theme-light.png", 0, q("theme-light")}};
    check(!onlyLight.resolve("Assets/Icon.png", context("en-US", 1.0f, ResourceTheme::kDark)),
          "theme: a light-only resource does not serve dark");
    ResourceIndex onlyHigh = index;
    onlyHigh.candidates["Assets/Icon.png"] = {
        {CandidateKind::kFile, "Assets/Icon.contrast-high.png", 0, q("contrast-high")}};
    check(!onlyHigh.resolve("Assets/Icon.png", context("en-US")),
          "contrast: a high-contrast resource does not serve a normal context");

    // Language dominates scale: an exact-language candidate at the wrong scale beats a neutral one at
    // the right scale.
    index.addFile("Assets/Banner.png", "Assets/Banner.scale-200.png", q("scale-200"));
    index.addFile("Assets/Banner.png", "Assets/lang-zh-CN/Banner.png", q("lang-zh-CN"));
    check(index.resolve("Assets/Banner.png", context("zh-CN", 2.0f))->path == "Assets/lang-zh-CN/Banner.png",
          "language outranks scale");
    check(index.resolve("Assets/Banner.png", context("en-US", 2.0f))->path == "Assets/Banner.scale-200.png",
          "another language falls back to the neutral candidate");
}

void indexFormat()
{
    ResourceIndex index;
    index.name = "App";
    index.defaultLanguage = "en-US";
    index.addFile("Assets/Logo.png", "Assets/Logo.scale-200.png", q("scale-200"));
    index.addString("Resources/Greeting", "Hello", q("lang-en-US"));
    index.addString("Resources/Greeting", "你好", q("lang-zh-CN"));
    check(index.validate().empty(), "a built index validates");
    const auto bytes = writeVpri(index);
    check(writeVpri(index) == bytes, "writing an index is deterministic");
    const auto read = readVpri(bytes);
    const auto* back = std::get_if<ResourceIndex>(&read);
    check(back && back->name == "App" && back->candidates.size() == 2 &&
              back->blobOf(*back->resolve("Resources/Greeting", context("zh-CN"))) == "你好",
          "an index round-trips");

    const auto rejects = [](std::vector<u8> b, std::string_view expected)
    {
        const auto r = readVpri(b);
        const auto* e = std::get_if<VpriError>(&r);
        if (e && e->message.find(expected) == std::string::npos)
            std::printf("  (got: %s)\n", e->message.c_str());
        return e && e->message.find(expected) != std::string::npos;
    };
    auto flipped = bytes;
    flipped[flipped.size() - 3] ^= 0x10;
    check(rejects(flipped, "checksum"), "a damaged index is rejected");
    check(rejects({bytes.begin(), bytes.begin() + 40}, "checksum"), "a truncated index is rejected");
    auto magic = bytes;
    magic[1] = 'X';
    check(rejects(magic, "magic"), "a foreign file is not an index");
    auto version = bytes;
    version[4] = 7;
    check(rejects(version, "unsupported version"), "another major version is rejected");

    ResourceIndex duplicate = index;
    duplicate.addFile("Assets/Logo.png", "Assets/Logo.scale-200.png", q("scale-200"));
    check(rejects(writeVpri(duplicate), "duplicate candidate"), "a duplicate candidate is rejected");
    ResourceIndex escaping = index;
    escaping.addFile("Assets/Evil.png", "../outside.png");
    check(rejects(writeVpri(escaping), "not a normalized resource path"),
          "a candidate outside the package is rejected");
    check(dumpVpri(*back, nullptr).find("Resources/Greeting") != std::string::npos,
          "dump lists logical paths");
    const auto ctx = context("zh-CN");
    check(dumpVpri(*back, &ctx).find("lang-zh-CN") != std::string::npos &&
              dumpVpri(*back, &ctx).find("<- resolved") != std::string::npos,
          "dump with a context marks the winning candidate");
}

void deflate()
{
    // A zlib (wbits=-15, level 9) dynamic-Huffman stream: inflate reads other encoders' output.
    const std::vector<u8> zlibStream = {
        0xed, 0xcd, 0x6b, 0x0e, 0x84, 0x20, 0x0c, 0x04, 0xe0, 0xab, 0xf4, 0x6a, 0x93, 0x32, 0x22, 0x91,
        0x85, 0xb5, 0x80, 0xf1, 0xf8, 0xc6, 0xb8, 0x1a, 0xcf, 0xb0, 0xe9, 0xef, 0x79, 0x7c, 0xc6, 0x56,
        0x87, 0x29, 0xa5, 0xcf, 0xfc, 0x50, 0xd6, 0x81, 0x9c, 0xa6, 0x44, 0x93, 0x54, 0x02, 0x77, 0xd9,
        0x98, 0xab, 0x41, 0x9a, 0x22, 0xbf, 0xc3, 0x2f, 0x74, 0x41, 0xa4, 0x68, 0x2d, 0xdd, 0xd0, 0xfa,
        0xaf, 0xa0, 0x28, 0x21, 0x05, 0x74, 0x8a, 0xdd, 0xb7, 0x4f, 0x23, 0xa3, 0xc4, 0x71, 0x6e, 0xae,
        0x63, 0x73, 0xd7, 0x5d, 0x77, 0xdd, 0x75, 0xd7, 0x5d, 0x77, 0xdd, 0xfd, 0x4f, 0xf7, 0x00};
    const auto inflated = inflateRaw(zlibStream, 2399);
    check(inflated &&
              std::string(inflated->begin(), inflated->begin() + 31) == "resource theme qualifier index ",
          "a zlib dynamic-Huffman stream inflates");
    check(!inflateRaw(zlibStream, 2398) && !inflateRaw(zlibStream, 2400),
          "inflate refuses a stream that does not decode to the expected size");
    auto damaged = zlibStream;
    damaged[20] ^= 0xFF;
    const auto bad = inflateRaw(damaged, 2399);
    check(!bad || *bad != *inflated, "a damaged stream never inflates to the original");

    std::mt19937 random(7);
    std::vector<u8> noise(5000);
    for (auto& b : noise)
        b = static_cast<u8>(random());
    std::string text;
    for (int i = 0; i < 400; ++i)
        text += "<TextBlock Text=\"{x:Bind Greeting}\" Margin=\"4\"/>\n";
    for (const auto& input : {noise, bytesOf(text), std::vector<u8>{}, bytesOf("a")})
    {
        const auto packed = deflateRaw(input);
        const auto unpacked = inflateRaw(packed, input.size());
        check(unpacked && *unpacked == input, "deflate round-trips");
    }
    check(deflateRaw(bytesOf(text)).size() * 10 < text.size(), "repetitive markup compresses");
}

void package()
{
    TempDir dir;
    ResourceIndex index;
    index.name = "App";
    index.defaultLanguage = "en-US";
    index.addFile("Assets/Logo.png", "Assets/Logo.scale-100.png", q("scale-100"));
    index.addFile("Assets/Logo.png", "Assets/Logo.scale-200.png", q("scale-200"));
    index.addString("Resources/Greeting", "Hello", q("lang-en-US"));
    std::string markup;
    for (int i = 0; i < 200; ++i)
        markup += "<Button Content=\"OK\"/>";

    PackageWriter writer({"App", "1.0.0", "en-US"});
    check(writer.add("Assets/Logo.scale-100.png", bytesOf("logo@100")), "a payload is added");
    check(writer.add("Assets/Logo.scale-200.png", bytesOf("logo@200")), "a second payload is added");
    check(writer.add("Views/Main.vxbf", bytesOf(markup), true), "a compressed payload is added");
    check(!writer.add("Views/Main.vxbf", bytesOf("again")) && !writer.add("../x", bytesOf("x")),
          "a duplicate or escaping entry path is refused");
    writer.setIndex(index);
    auto finished = writer.finish();
    check(std::holds_alternative<std::vector<u8>>(finished), "a consistent package is written");
    const auto bytes = std::get<std::vector<u8>>(finished);
    check(std::get<std::vector<u8>>(writer.finish()) == bytes, "writing a package is deterministic");
    const auto file = dir.path / "App.vpak";
    std::ofstream(file, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    auto opened = PackageFile::open(file);
    check(std::holds_alternative<std::shared_ptr<PackageFile>>(opened), "the package opens");
    if (!std::holds_alternative<std::shared_ptr<PackageFile>>(opened))
    {
        std::printf("  %s\n", std::get<VpakError>(opened).message.c_str());
        return;
    }
    const auto pak = std::get<std::shared_ptr<PackageFile>>(opened);
    check(pak->manifest().name == "App" && pak->entries().size() == 3, "manifest and directory are read");
    check(pak->find("Views/Main.vxbf")->compression == PakCompression::kDeflate &&
              pak->find("Assets/Logo.scale-100.png")->compression == PakCompression::kStored,
          "compression is a per-entry flag");
    check(pak->fileReads() == 0, "opening reads no payload");
    check(pak->read("Views/Main.vxbf") == bytesOf(markup), "a compressed payload reads back");
    check(pak->fileReads() == 1 && pak->read("Views/Main.vxbf") && pak->fileReads() == 1,
          "payloads are read on demand and cached");
    check(pak->verify().empty(), "an intact package verifies");
    check(pak->dump().find("deflate") != std::string::npos, "dump lists entries with compression");

    ResourceManager manager;
    manager.mount(ResourceNamespace::kApplication, std::make_shared<PackageFileProvider>(pak));
    const auto uri = *ResourceUri::parse("ms-appx:///Assets/Logo.png");
    check(manager.resolve(uri, context("en-US", 2.0f))->bytes == bytesOf("logo@200") &&
              manager.resolve(uri, context("en-US", 1.0f))->bytes == bytesOf("logo@100"),
          "a packaged image resolves per scale");
    check(manager.string(*ResourceUri::parse("ms-appx:///Resources/Greeting"), context("en-US")) == "Hello",
          "a packaged string resolves");

    // Damage: a payload byte, the metadata, the header.
    const auto corrupt = [&](usize at, const char* name)
    {
        auto copy = bytes;
        copy[at] ^= 0x5A;
        const auto path = dir.path / name;
        std::ofstream(path, std::ios::binary)
            .write(reinterpret_cast<const char*>(copy.data()), static_cast<std::streamsize>(copy.size()));
        return PackageFile::open(path);
    };
    auto payloadDamaged = corrupt(bytes.size() - 1, "payload.vpak");
    check(std::holds_alternative<std::shared_ptr<PackageFile>>(payloadDamaged),
          "payload damage is found on read, not at open");
    if (const auto* p = std::get_if<std::shared_ptr<PackageFile>>(&payloadDamaged))
    {
        const auto& last = (*p)->entries().back();
        check(!(*p)->read(last.path) && (*p)->verify().size() == 1,
              "a damaged payload reads as a miss and fails verify");
    }
    // A stored (uncompressed) payload: only the directory checksum can notice the change.
    u64 payloadArea = bytes.size();
    for (const auto& entry : pak->entries())
        payloadArea -= entry.storedSize;
    const auto* stored = pak->find("Assets/Logo.scale-100.png");
    auto storedDamaged = corrupt(static_cast<usize>(payloadArea + stored->offset), "stored.vpak");
    if (const auto* p = std::get_if<std::shared_ptr<PackageFile>>(&storedDamaged))
        check(!(*p)->read(stored->path) && (*p)->read("Assets/Logo.scale-200.png") == bytesOf("logo@200"),
              "a damaged stored payload is a miss; its neighbours still read");
    else
        check(false, "a damaged stored payload still opens");
    const auto openError = [](const auto& r) -> std::string
    {
        const auto* e = std::get_if<VpakError>(&r);
        return e ? e->message : std::string();
    };
    check(openError(corrupt(70, "meta.vpak")).find("metadata checksum") != std::string::npos,
          "damaged metadata is rejected at open");
    check(openError(corrupt(13, "header.vpak")).find("header checksum") != std::string::npos,
          "a damaged header is rejected at open");
    check(openError(corrupt(0, "magic.vpak")).find("bad magic") != std::string::npos,
          "a foreign file is rejected");

    PackageWriter inconsistent({"Bad", "1", "en-US"});
    inconsistent.add("Assets/Logo.scale-100.png", bytesOf("x"));
    inconsistent.setIndex(index); // names Assets/Logo.scale-200.png, which this package lacks
    const auto result = inconsistent.finish();
    const auto* failure = std::get_if<VpakError>(&result);
    check(failure && failure->message.find("not in the package") != std::string::npos,
          "an index naming another package's file is refused");
}

void looseAndPacked()
{
    TempDir dir;
    const fs::path root = dir.path / "app";
    const auto put = [&](const std::string& p, std::string_view text) { dir.write("app/" + p, text); };
    put("Assets/Logo.scale-100.png", "logo100");
    put("Assets/Logo.scale-150.png", "logo150");
    put("Assets/Logo.scale-200.png", "logo200");
    put("Assets/Logo.theme-dark_scale-200.png", "logoDark200");
    put("Assets/lang-zh-CN/Banner.png", "bannerZh");
    put("Assets/Banner.png", "banner");
    put("Fonts/Velora.ttf", "font");
    put("Strings/en-US/Resources.resw", "<root/>"); // skipped by the scanner; velorac compiles it

    auto scanned = indexResourceFiles(root, "App", "en-US");
    check(std::holds_alternative<ResourceIndex>(scanned), "the directory indexes");
    if (!std::holds_alternative<ResourceIndex>(scanned))
    {
        std::printf("  %s\n", std::get<ResourceScanError>(scanned).message.c_str());
        return;
    }
    auto index = std::get<ResourceIndex>(scanned);
    check(index.variants("Assets/Logo.png") && index.variants("Assets/Logo.png")->size() == 4 &&
              index.variants("Assets/Banner.png") && index.variants("Fonts/Velora.ttf") &&
              !index.variants("Strings/en-US/Resources.resw"),
          "file names and directories map to logical paths and qualifiers");
    check(addReswStrings(index, "Resources", "en-US", {{"Greeting", "Hello"}, {"Farewell", "Bye"}}).empty() &&
              addReswStrings(index, "Resources", "zh-CN", {{"Greeting", "你好"}}).empty(),
          ".resw entries become string candidates");
    check(!addReswStrings(index, "Resources", "en-US", {{"Greeting", "again"}}).empty() &&
              !addReswStrings(index, "Resources", "en-US", {{"a/b", "x"}}).empty(),
          "a duplicate string or a bad key is an error");

    PackageWriter writer({"App", "1.0.0", "en-US"});
    for (const auto& [logical, variants] : index.candidates)
        for (const auto& candidate : variants)
            if (candidate.kind == CandidateKind::kFile)
            {
                std::ifstream in(root / candidate.path, std::ios::binary);
                const std::string text{std::istreambuf_iterator<char>(in), {}};
                writer.add(candidate.path, bytesOf(text), true);
            }
    writer.setIndex(index);
    const auto bytes = std::get<std::vector<u8>>(writer.finish());
    const auto file = dir.path / "App.vpak";
    std::ofstream(file, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto pak = std::get<std::shared_ptr<PackageFile>>(PackageFile::open(file));

    ResourceManager loose, packed;
    loose.mount(ResourceNamespace::kApplication,
                std::make_shared<LooseFileProvider>(root, std::make_shared<const ResourceIndex>(index)));
    packed.mount(ResourceNamespace::kApplication, std::make_shared<PackageFileProvider>(pak));

    bool same = true;
    for (const char* language : {"en-US", "zh-CN", "fr-FR"})
        for (const f32 scale : {1.0f, 1.25f, 1.5f, 2.0f, 3.0f})
            for (const auto theme :
                 {ResourceTheme::kLight, ResourceTheme::kDark, ResourceTheme::kHighContrast})
                for (const char* path : {"Assets/Logo.png", "Assets/Banner.png", "Fonts/Velora.ttf",
                                         "Resources/Greeting", "Resources/Farewell"})
                {
                    const auto uri = *ResourceUri::fromPath(ResourceNamespace::kApplication, path);
                    const auto c = context(language, scale, theme);
                    const auto a = loose.resolve(uri, c), b = packed.resolve(uri, c);
                    same = same && a.has_value() == b.has_value() && (!a || a->bytes == b->bytes);
                }
    check(same, "loose and packed resources resolve identically in every context");

    const auto logo = *ResourceUri::parse("ms-appx:///Assets/Logo.png");
    check(packed.resolve(logo, context("en-US", 2.0f, ResourceTheme::kDark))->bytes ==
                  bytesOf("logoDark200") &&
              packed.resolve(logo, context("en-US", 1.5f))->bytes == bytesOf("logo150"),
          "theme and scale pick the variant");
    const auto greeting = *ResourceUri::parse("ms-appx:///Resources/Greeting");
    check(packed.string(greeting, context("zh-CN")) == "你好" &&
              packed.string(greeting, context("en-US")) == "Hello",
          "strings resolve per language");
    check(!packed.string(greeting, context("fr-FR")) &&
              !packed.string(*ResourceUri::parse("ms-appx:///Resources/Farewell"), context("zh-CN")),
          "a string without a candidate for the language is a miss, not another language's text");

    // Two roots, two contexts, one shared index: each resolves its own candidate.
    const auto windowA = context("en-US", 1.0f), windowB = context("zh-CN", 2.0f);
    const auto banner = *ResourceUri::parse("ms-appx:///Assets/Banner.png");
    check(packed.resolve(banner, windowA)->bytes == bytesOf("banner") &&
              packed.resolve(banner, windowB)->bytes == bytesOf("bannerZh") &&
              packed.resolve(logo, windowA)->bytes == bytesOf("logo100") &&
              packed.resolve(logo, windowB)->bytes == bytesOf("logo200"),
          "two root contexts resolve the same URI independently");

    // Bad names in a resource tree are reported, not indexed as ordinary files.
    put("Assets/Icon.scale-big.png", "x");
    check(std::holds_alternative<ResourceScanError>(indexResourceFiles(root, "App", "en-US")),
          "a malformed qualifier in a file name is an error");
    fs::remove(root / "Assets/Icon.scale-big.png");
    put("Assets/scale-200/Logo.png", "dup");
    check(std::holds_alternative<ResourceScanError>(indexResourceFiles(root, "App", "en-US")),
          "two files for the same resource and qualifier are an error");
}

void embeddedPackage()
{
    ResourceIndex index;
    index.name = "Embedded";
    index.defaultLanguage = "en-US";
    index.addFile("Views/MainPage.vxbf", "Views/MainPage.vxbf");
    PackageWriter writer({"Embedded", "1.0.0", "en-US"});
    const auto bytes = bytesOf("compiled-XAML-placeholder");
    check(writer.add("Views/MainPage.vxbf", bytes), "embedded payload accepted");
    writer.setIndex(std::move(index));
    const auto packed = writer.finish();
    check(std::holds_alternative<std::vector<u8>>(packed), "embedded package can be built");
    if (const auto* image = std::get_if<std::vector<u8>>(&packed))
    {
        auto opened = PackageFile::openMemory(*image);
        const auto* package = std::get_if<std::shared_ptr<PackageFile>>(&opened);
        check(package && (*package)->read("Views/MainPage.vxbf") == bytes,
              "embedded .vpak can resolve a compiled XAML payload");
        ResourceManager manager;
        check(manager.mountEmbeddedPackage(ResourceNamespace::kApplication, *image).empty(),
              "embedded package mounts without a temporary file");
        const auto uri = *ResourceUri::parse("ms-appx:///Views/MainPage.vxbf");
        check(manager.read(uri) == bytes && manager.resolve(uri, context("en-US"))->bytes == bytes,
              "embedded package resolves physical and logical resource identifiers");
        auto broken = *image;
        broken[0] ^= 1;
        check(std::holds_alternative<VpakError>(PackageFile::openMemory(broken)),
              "invalid embedded package rejected");
    }
}

void mounting()
{
    auto app = std::make_shared<MemoryResourceProvider>();
    auto fluent = std::make_shared<MemoryResourceProvider>();
    auto appOverride = std::make_shared<MemoryResourceProvider>();
    static const std::vector<u8> appStyle = bytesOf("app"), fluentStyle = bytesOf("fluent"),
                                 overrideStyle = bytesOf("override");
    app->add("Themes/Generic.vxbf", appStyle);
    fluent->add("Themes/Generic.vxbf", fluentStyle);
    appOverride->add("Themes/Generic.vxbf", overrideStyle);

    ResourceManager manager;
    manager.mount(ResourceNamespace::kApplication, appOverride);
    manager.mount(ResourceNamespace::kApplication, app);
    manager.mount(ResourceNamespace::kFramework, fluent);
    const auto c = context("en-US");
    check(manager.resolve(*ResourceUri::parse("ms-appx:///Themes/Generic.vxbf"), c)->bytes == overrideStyle,
          "the package mounted first overrides a later one");
    check(manager.resolve(*ResourceUri::parse("ms-appx-framework:///Themes/Generic.vxbf"), c)->bytes ==
              fluentStyle,
          "framework and application namespaces are separate");
    check(!manager.resolve(*ResourceUri::parse("ms-loose:///Themes/Generic.vxbf"), c),
          "a namespace with nothing mounted resolves nothing");
    check(!ResourceUri::parse("ms-appx:///../Themes/Generic.vxbf") && !ResourceUri::parse("http://x/y"),
          "escaping paths and foreign schemes are rejected before any provider is asked");

    // A package whose index owns a path is not mixed with a later package's variants.
    ResourceIndex onlyDark;
    onlyDark.name = "Dark";
    onlyDark.defaultLanguage = "en-US";
    onlyDark.addString("Resources/Title", "dark title", q("theme-dark"));
    struct IndexOnly final : ResourceProvider
    {
        ResourceIndex value;
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
            return &value;
        }
    };
    auto first = std::make_shared<IndexOnly>();
    first->value = onlyDark;
    auto fallback = std::make_shared<MemoryResourceProvider>();
    static const std::vector<u8> plain = bytesOf("plain title");
    fallback->add("Resources/Title", plain);
    ResourceManager layered;
    layered.mount(ResourceNamespace::kApplication, first);
    layered.mount(ResourceNamespace::kApplication, fallback);
    const auto title = *ResourceUri::parse("ms-appx:///Resources/Title");
    check(layered.string(title, context("en-US", 1.0f, ResourceTheme::kDark)) == "dark title" &&
              !layered.string(title, context("en-US")),
          "a package that owns a path is overridden as a whole, never mixed with another package");
}
} // namespace

int main()
{
    qualifiers();
    ranking();
    indexFormat();
    deflate();
    package();
    embeddedPackage();
    looseAndPacked();
    mounting();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
