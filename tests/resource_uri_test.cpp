// ResourceUri (R01): path normalization and the rule that a resource reference never escapes
// its package root. These are the security-relevant cases R08.1 requires ("reject ../",
// "normalize paths", "separate URI spaces"); R08 builds providers on top of this type.
#include <cstdio>
#include <string>
#include <velora/resources/ResourceUri.hpp>

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

bool normalizesTo(std::string_view in, std::string_view expected)
{
    const auto out = normalizeResourcePath(in);
    return out && *out == expected;
}

bool rejected(std::string_view in)
{
    return !normalizeResourcePath(in).has_value();
}
} // namespace

int main()
{
    // Normalization: separators, repeated and trailing slashes, "." segments.
    check(normalizesTo("Views/MainPage.xaml", "Views/MainPage.xaml"), "plain path is unchanged");
    check(normalizesTo("Views\\MainPage.xaml", "Views/MainPage.xaml"), "backslashes become slashes");
    check(normalizesTo("Views//MainPage.xaml", "Views/MainPage.xaml"), "repeated slashes collapse");
    check(normalizesTo("Views/./MainPage.xaml", "Views/MainPage.xaml"), "'.' segments collapse");
    check(normalizesTo("Views/MainPage.xaml/", "Views/MainPage.xaml"), "trailing slash is dropped");
    check(normalizesTo("a/b/../c", "a/c"), "'..' cancels the previous segment inside the root");

    // Escaping the root is never allowed, however it is spelled.
    check(rejected("../secret.txt"), "leading '..' is rejected");
    check(rejected("a/../../secret.txt"), "'..' climbing above the root is rejected");
    check(rejected("a/b/../../../x"), "deep escape is rejected");
    check(rejected("..\\secret.txt"), "backslash '..' is rejected");
    check(rejected("/etc/passwd"), "absolute POSIX path is rejected");
    check(rejected("\\Windows\\win.ini"), "rooted Windows path is rejected");
    check(rejected("C:/Windows/win.ini"), "drive path is rejected");
    check(rejected("C:win.ini"), "drive-relative path is rejected");
    check(rejected(""), "empty path is rejected");
    check(rejected("./."), "a path that normalizes to nothing is rejected");
    check(rejected("a/.."), "a path that cancels to the root itself is rejected");

    // URI schemes select the namespace; unknown schemes are rejected, not guessed at.
    const auto app = ResourceUri::parse("ms-appx:///Views/MainPage.xaml");
    check(app && app->nameSpace() == ResourceNamespace::kApplication && app->path() == "Views/MainPage.xaml",
          "ms-appx:/// parses into the application namespace");
    const auto framework = ResourceUri::parse("ms-appx-framework:///Themes/generic.xaml");
    check(framework && framework->nameSpace() == ResourceNamespace::kFramework &&
              framework->path() == "Themes/generic.xaml",
          "ms-appx-framework:/// parses into the framework namespace");
    const auto loose = ResourceUri::parse("ms-loose:///Assets/logo.png");
    check(loose && loose->nameSpace() == ResourceNamespace::kLoose,
          "ms-loose:/// parses into the loose namespace");
    const auto bare = ResourceUri::parse("Assets/logo.png");
    check(bare && bare->nameSpace() == ResourceNamespace::kApplication,
          "a bare path is an application resource");
    check(!ResourceUri::parse("https://example.com/x.xaml"), "an unknown scheme is rejected");
    check(!ResourceUri::parse("file:///C:/x.xaml"), "file:// is rejected");
    check(!ResourceUri::parse("ms-appx:///../escape.xaml"), "a scheme does not make '..' escape legal");

    // Namespaces keep identical paths distinct: the app's Button style must not alias the
    // framework's.
    const auto appButton = ResourceUri::parse("ms-appx:///Styles/Button.xaml");
    const auto fwButton = ResourceUri::parse("ms-appx-framework:///Styles/Button.xaml");
    check(appButton && fwButton && appButton->path() == fwButton->path() && !(*appButton == *fwButton),
          "the same path in two namespaces is two different URIs");

    // Round trip through the canonical spelling.
    check(app && app->toString() == "ms-appx:///Views/MainPage.xaml", "canonical spelling round-trips");
    const auto reparsed = app ? ResourceUri::parse(app->toString()) : std::nullopt;
    check(reparsed && app && *reparsed == *app, "parse(toString()) is the identity");
    check(ResourceUri{}.empty() && ResourceUri{}.toString().empty(), "a default URI is empty");

    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
