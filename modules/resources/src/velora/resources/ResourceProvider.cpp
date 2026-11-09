#include <algorithm>
#include <array>
#include <fstream>
#include <variant>
#include <velora/resources/PackageFile.hpp>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <iterator>
#include <velora/resources/ResourceIndex.hpp>
#include <velora/resources/ResourceProvider.hpp>

namespace velora
{

void MemoryResourceProvider::add(std::string path, std::span<const u8> bytes)
{
    std::lock_guard lock(m_mutex);
    m_entries.insert_or_assign(std::move(path), bytes);
}

std::optional<std::vector<u8>> MemoryResourceProvider::read(const std::string& path) const
{
    std::lock_guard lock(m_mutex);
    const auto it = m_entries.find(path);
    if (it == m_entries.end())
        return std::nullopt;
    return std::vector<u8>(it->second.begin(), it->second.end());
}

bool MemoryResourceProvider::contains(const std::string& path) const
{
    std::lock_guard lock(m_mutex);
    return m_entries.contains(path);
}

LooseFileProvider::LooseFileProvider(std::filesystem::path root, std::shared_ptr<const ResourceIndex> index)
    : m_root(std::filesystem::weakly_canonical(std::move(root))), m_index(std::move(index))
{
}

std::optional<std::filesystem::path> LooseFileProvider::resolve(const std::string& path) const
{
    // The path is a normalized ResourceUri path; check again against symlinks and odd spellings.
    const auto candidate = std::filesystem::weakly_canonical(m_root / std::filesystem::path(path));
    const auto relative = candidate.lexically_relative(m_root);
    if (relative.empty() || relative.native().starts_with(std::filesystem::path("..").native()))
        return std::nullopt;
    return candidate;
}

std::optional<std::vector<u8>> LooseFileProvider::read(const std::string& path) const
{
    const auto file = resolve(path);
    if (!file)
        return std::nullopt;
    std::ifstream in(*file, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::vector<u8>(std::istreambuf_iterator<char>(in), {});
}

bool LooseFileProvider::contains(const std::string& path) const
{
    const auto file = resolve(path);
    std::error_code error;
    return file && std::filesystem::is_regular_file(*file, error);
}

std::string ResourceManager::mountEmbeddedPackage(ResourceNamespace nameSpace, std::span<const u8> bytes)
{
    auto opened = PackageFile::openMemory(bytes);
    if (const auto* error = std::get_if<VpakError>(&opened))
        return error->message;
    mount(nameSpace, std::make_shared<PackageFileProvider>(std::get<std::shared_ptr<PackageFile>>(opened)));
    return {};
}

std::string ResourceManager::mountExternalPackage(ResourceNamespace nameSpace, std::string_view fileName)
{
    // External packages may only be referenced by file name: no path traversal or CWD dependence.
    const std::filesystem::path file(fileName);
    if (file.empty() || file.has_parent_path() || file == "." || file == "..")
        return "external resource package name must be a plain file name";
    std::filesystem::path executable;
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size())
        return "could not locate executable";
    executable = buffer.substr(0, count);
#elif defined(__linux__)
    std::array<char, 4096> buffer{};
    const ssize_t count = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (count <= 0 || static_cast<usize>(count) >= buffer.size())
        return "could not locate executable";
    executable = std::string(buffer.data(), static_cast<usize>(count));
#elif defined(__APPLE__)
    std::array<char, 4096> buffer{};
    u32 size = static_cast<u32>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return "could not locate executable";
    executable = buffer.data();
#else
    return "unsupported platform";
#endif
    auto opened = PackageFile::open(executable.parent_path() / "Resources" / file);
    if (const auto* error = std::get_if<VpakError>(&opened))
        return error->message;
    mount(nameSpace, std::make_shared<PackageFileProvider>(std::get<std::shared_ptr<PackageFile>>(opened)));
    return {};
}

void ResourceManager::mount(ResourceNamespace nameSpace, std::shared_ptr<const ResourceProvider> provider)
{
    std::lock_guard lock(m_mutex);
    m_mounts.emplace(nameSpace, std::move(provider));
}

std::optional<std::vector<u8>> ResourceManager::read(const ResourceUri& uri) const
{
    if (uri.empty())
        return std::nullopt;
    std::lock_guard lock(m_mutex);
    const auto [first, last] = m_mounts.equal_range(uri.nameSpace());
    for (auto it = first; it != last; ++it)
        if (auto bytes = it->second->read(uri.path()))
            return bytes;
    return std::nullopt;
}

bool ResourceManager::contains(const ResourceUri& uri) const
{
    std::lock_guard lock(m_mutex);
    const auto [first, last] = m_mounts.equal_range(uri.nameSpace());
    for (auto it = first; it != last; ++it)
        if (it->second->contains(uri.path()))
            return true;
    return false;
}

std::optional<ResolvedResource> ResourceManager::resolve(const ResourceUri& uri,
                                                         const ResourceContext& context) const
{
    if (uri.empty())
        return std::nullopt;
    std::lock_guard lock(m_mutex);
    const auto [first, last] = m_mounts.equal_range(uri.nameSpace());
    for (auto it = first; it != last; ++it)
    {
        const ResourceProvider& provider = *it->second;
        ResolvedResource out;
        out.provider = provider.name();
        const ResourceIndex* index = provider.index();
        if (index && index->variants(uri.path()))
        {
            const ResourceCandidate* candidate = index->resolve(uri.path(), context);
            if (!candidate)
                return std::nullopt; // the package owns the path, but no variant fits this context
            out.qualifier = candidate->qualifier;
            if (candidate->kind == CandidateKind::kFile)
            {
                auto bytes = provider.read(candidate->path);
                if (!bytes)
                    return std::nullopt; // a damaged or missing payload is a miss, never another file
                out.bytes = std::move(*bytes);
                out.physicalPath = candidate->path;
            }
            else
            {
                const auto blob = index->blobOf(*candidate);
                out.bytes.assign(blob.begin(), blob.end());
                out.physicalPath = uri.path();
            }
            return out;
        }
        if (auto bytes = provider.read(uri.path()))
        {
            out.bytes = std::move(*bytes);
            out.physicalPath = uri.path();
            return out;
        }
    }
    return std::nullopt;
}

std::vector<std::string> ResourceManager::paths(ResourceNamespace nameSpace, std::string_view prefix) const
{
    std::vector<std::string> out;
    std::lock_guard lock(m_mutex);
    const auto [first, last] = m_mounts.equal_range(nameSpace);
    for (auto it = first; it != last; ++it)
        if (const ResourceIndex* index = it->second->index())
            for (auto c = index->candidates.lower_bound(prefix);
                 c != index->candidates.end() && c->first.starts_with(prefix); ++c)
                if (std::ranges::find(out, c->first) == out.end())
                    out.push_back(c->first);
    return out;
}

std::optional<std::string> ResourceManager::string(const ResourceUri& uri,
                                                   const ResourceContext& context) const
{
    auto resolved = resolve(uri, context);
    if (!resolved)
        return std::nullopt;
    return std::string(resolved->bytes.begin(), resolved->bytes.end());
}

MemoryResourceProvider& ResourceManager::embedded()
{
    static MemoryResourceProvider provider;
    return provider;
}

ResourceManager& ResourceManager::instance()
{
    static ResourceManager manager;
    // Non-owning: the embedded provider is a function-local static that outlives every use.
    static const bool mounted =
        (manager.mount(ResourceNamespace::kApplication,
                       std::shared_ptr<const ResourceProvider>(std::shared_ptr<void>(), &embedded())),
         true);
    (void)mounted;
    return manager;
}

} // namespace velora
