#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <velora/globals.hpp>

// Resource identity: a normalized, provider-independent path into one resource namespace.
//
// A ResourceUri is not a filesystem path. It is the lookup key a ResourceProvider resolves,
// and the key a .vpri index is written against. Two rules are enforced at construction:
// the path is normalized (forward slashes, no "." or ".." segments) and it is never absolute,
// so a resource reference can never escape its package root.
//
// The namespace separates resources that may shadow each other: the framework's own Fluent
// package and the application's package both provide, for example, a "Button" style, and the
// application's must win without either being able to reach the other's files.

namespace velora
{

enum class ResourceNamespace : u8
{
    kApplication, // the app package: wins over the framework for the same path
    kFramework,   // the Fluent package shipped with Velora
    kLoose        // files next to the executable, for development and installed apps
};

// Returns the normalized form of `path`, or nullopt when it is not a valid resource path
// (absolute, a drive or UNC root, or containing a ".." segment that would escape the root).
[[nodiscard]] std::optional<std::string> normalizeResourcePath(std::string_view path);

// A normalized resource path inside one namespace. Construct with parse(); a default-constructed
// ResourceUri is empty and never resolves.
class ResourceUri final
{
public:
    ResourceUri() = default;

    // Parses "ms-appx:///Views/MainPage.xaml" or a bare "Views/MainPage.xaml" (kApplication).
    // The recognized schemes are "ms-appx://" (kApplication), "ms-appx-framework://" (kFramework)
    // and "ms-loose://" (kLoose); anything else is rejected rather than guessed at.
    [[nodiscard]] static std::optional<ResourceUri> parse(std::string_view text);

    // Builds a URI from an already-validated relative path.
    [[nodiscard]] static std::optional<ResourceUri> fromPath(ResourceNamespace nameSpace,
                                                             std::string_view relativePath);

    [[nodiscard]] bool empty() const noexcept
    {
        return m_path.empty();
    }
    [[nodiscard]] ResourceNamespace nameSpace() const noexcept
    {
        return m_nameSpace;
    }
    // Normalized, relative, '/'-separated. Empty when this URI is empty.
    [[nodiscard]] const std::string& path() const noexcept
    {
        return m_path;
    }
    // The canonical "scheme:///path" spelling. Empty when this URI is empty.
    [[nodiscard]] std::string toString() const;

    friend bool operator==(const ResourceUri&, const ResourceUri&) noexcept = default;

private:
    ResourceNamespace m_nameSpace = ResourceNamespace::kApplication;
    std::string m_path;
};

} // namespace velora
