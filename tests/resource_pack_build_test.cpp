// R08 end to end: the package velora_add_xaml(... RESOURCES) built from tests/resources/app with
// velorac, opened and mounted the way an application does it; the same tree served loose with the
// package's index; and two XamlRoots with different contexts resolving the same URIs.
#include <cstdio>
#include <memory>
#include <string>
#include <velora/application/XamlRoot.hpp>
#include <velora/resources/PackageFile.hpp>
#include <velora/resources/ResourceProvider.hpp>

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

std::string text(const std::optional<ResolvedResource>& resource)
{
    return resource ? std::string(resource->bytes.begin(), resource->bytes.end()) : std::string("<miss>");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::printf("usage: velora_resource_pack_build_test <App.vpak> <resource root>\n");
        return 2;
    }
    auto opened = PackageFile::open(argv[1]);
    if (const auto* error = std::get_if<VpakError>(&opened))
    {
        std::printf("FAIL: %s\n", error->message.c_str());
        return 1;
    }
    const auto package = std::get<std::shared_ptr<PackageFile>>(opened);
    check(package->manifest().name == "App" && package->verify().empty(),
          "the built package opens and verifies");
    check(package->find("Strings/en-US/Resources.resw") == nullptr,
          ".resw sources are compiled, not packaged");

    ResourceManager packed, loose;
    packed.mount(ResourceNamespace::kApplication, std::make_shared<PackageFileProvider>(package));
    loose.mount(ResourceNamespace::kApplication,
                std::make_shared<LooseFileProvider>(argv[2],
                                                    std::make_shared<const ResourceIndex>(package->index())));

    const auto logo = *ResourceUri::parse("ms-appx:///Assets/Logo.txt");
    const auto banner = *ResourceUri::parse("ms-appx:///Assets/Banner.txt");
    const auto greeting = *ResourceUri::parse("ms-appx:///Resources/Greeting");
    const auto title = *ResourceUri::parse("ms-appx:///Resources/Title.Text");

    // Two windows: one at 100% in English, one at 150% (then 200%, then dark) in Chinese.
    XamlRoot english, chinese;
    english.setHostState(800, 600, 1.0f, true);
    chinese.setHostState(800, 600, 1.5f, true);
    chinese.setLanguage("zh-CN");
    check(text(english.resource(logo, packed)) == "logo@100" &&
              text(chinese.resource(logo, packed)) == "logo@150",
          "each root resolves the scale of its own monitor");
    check(english.string(greeting, packed) == "Hello" && chinese.string(greeting, packed) == "你好",
          "each root resolves its own language");
    check(english.string(title, packed) == "Settings & more", ".resw text is decoded");
    check(!chinese.string(title, packed), "a string missing in a language is a miss");
    check(text(english.resource(banner, packed)) == "banner" &&
              text(chinese.resource(banner, packed)) == "banner-zh",
          "a language directory qualifies files");
    chinese.setHostState(800, 600, 2.0f, true);
    check(text(chinese.resource(logo, packed)) == "logo@200", "a DPI change re-resolves");
    chinese.setTheme(Theme::kDark);
    check(text(chinese.resource(logo, packed)) == "logo-dark@200" &&
              text(english.resource(logo, packed)) == "logo@100",
          "a theme change affects only that root");

    bool same = true;
    for (const XamlRoot* root : {&english, &chinese})
        for (const auto& uri : {logo, banner, greeting, title})
        {
            const auto a = root->resource(uri, packed), b = root->resource(uri, loose);
            same = same && a.has_value() == b.has_value() && (!a || a->bytes == b->bytes);
        }
    check(same, "loose files with the package's index resolve like the package");

    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
