#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <velora/resources/ResourceContext.hpp>
#include <velora/resources/ResourceQualifier.hpp>
#include <velora/resources/ResourceUri.hpp>

// Resource providers (R08.1): where the bytes behind a ResourceUri come from.
//
//   MemoryResourceProvider  resources compiled into the executable (velorac embeds each .vxbf)
//   LooseFileProvider       files under a directory root; refuses anything that would leave it
//   PackageFileProvider     one .vpak (PackageFile.hpp)
//   ResourceManager         providers mounted per namespace in priority order; the first that has
//                           the path wins, so a package mounted earlier overrides a later one
//
// A provider may carry a .vpri index (R08.2). A lookup is then by *logical* path: the index picks
// the candidate for the ResourceContext and the same provider reads that candidate's payload.
// Candidates are only ever chosen within one provider, so a package never resolves to another
// package's file; a provider without an index serves its physical paths as they are.
//
// Paths are already normalized by ResourceUri (no "..", not absolute); providers never see raw
// strings, so no provider can be asked for a file outside its root.

namespace velora
{

struct ResourceIndex;

class ResourceProvider
{
public:
    virtual ~ResourceProvider() = default;
    // Physical paths: the provider's own files or entries.
    [[nodiscard]] virtual std::optional<std::vector<u8>> read(const std::string& path) const = 0;
    [[nodiscard]] virtual bool contains(const std::string& path) const = 0;
    // The provider's resource index, or null when it has none.
    [[nodiscard]] virtual const ResourceIndex* index() const
    {
        return nullptr;
    }
    // For diagnostics (candidate traces).
    [[nodiscard]] virtual std::string name() const
    {
        return {};
    }
};

class MemoryResourceProvider final : public ResourceProvider
{
public:
    // Registers bytes that live as long as the process (static arrays of generated code).
    void add(std::string path, std::span<const u8> bytes);
    std::optional<std::vector<u8>> read(const std::string& path) const override;
    bool contains(const std::string& path) const override;

private:
    mutable std::mutex m_mutex;
    std::map<std::string, std::span<const u8>, std::less<>> m_entries;
};

class LooseFileProvider final : public ResourceProvider
{
public:
    // `index`, when given, is the same .vpri a package of these files carries, so loose and packed
    // resources resolve identically (R08.5). Its file candidates are paths under `root`.
    explicit LooseFileProvider(std::filesystem::path root, std::shared_ptr<const ResourceIndex> index = {});
    std::optional<std::vector<u8>> read(const std::string& path) const override;
    bool contains(const std::string& path) const override;
    const ResourceIndex* index() const override
    {
        return m_index.get();
    }
    std::string name() const override
    {
        return m_root.generic_string();
    }

private:
    std::optional<std::filesystem::path> resolve(const std::string& path) const;
    std::filesystem::path m_root;
    std::shared_ptr<const ResourceIndex> m_index;
};

// What a context-aware lookup resolved to (R08.6 candidate tracing).
struct ResolvedResource
{
    std::vector<u8> bytes;
    std::string provider;     // ResourceProvider::name() of the provider that served it
    std::string physicalPath; // the candidate's path, or the logical path for embedded payloads
    ResourceQualifier qualifier;
};

class ResourceManager final
{
public:
    // Mounts `provider` for `nameSpace`; earlier mounts have priority.
    void mount(ResourceNamespace nameSpace, std::shared_ptr<const ResourceProvider> provider);
    // A compiled-in .vpak is validated and mounted without extracting it to disk.
    [[nodiscard]] std::string mountEmbeddedPackage(ResourceNamespace nameSpace, std::span<const u8> bytes);
    // Resolve a deployable .vpak relative to the executable's Resources directory.
    [[nodiscard]] std::string mountExternalPackage(ResourceNamespace nameSpace, std::string_view fileName);
    // Physical lookup: the first provider that has `uri.path()` as a file or entry.
    [[nodiscard]] std::optional<std::vector<u8>> read(const ResourceUri& uri) const;
    [[nodiscard]] bool contains(const ResourceUri& uri) const;
    // Logical lookup: the first provider (in mount order) that knows the path — through its index,
    // else as a physical path — serves it with its best candidate for `context`. Once a provider's
    // index knows the path, later providers are not consulted even when no candidate fits: a
    // package's resource is overridden as a whole, never mixed with another package's variants.
    [[nodiscard]] std::optional<ResolvedResource> resolve(const ResourceUri& uri,
                                                          const ResourceContext& context) const;
    // Logical paths starting with `prefix` in the indexes mounted for `nameSpace` (each once), e.g.
    // every "Resources/Greeting.*" an x:Uid applies.
    [[nodiscard]] std::vector<std::string> paths(ResourceNamespace nameSpace, std::string_view prefix) const;
    // A string resource (.resw entry) for `context`, e.g. "ms-appx:///Resources/Greeting".
    [[nodiscard]] std::optional<std::string> string(const ResourceUri& uri,
                                                    const ResourceContext& context) const;

    // The process-wide manager; its application namespace has the embedded provider mounted first.
    [[nodiscard]] static ResourceManager& instance();
    // Resources compiled into this executable (ms-appx:///...).
    [[nodiscard]] static MemoryResourceProvider& embedded();

private:
    mutable std::mutex m_mutex;
    std::multimap<ResourceNamespace, std::shared_ptr<const ResourceProvider>> m_mounts;
};

} // namespace velora
