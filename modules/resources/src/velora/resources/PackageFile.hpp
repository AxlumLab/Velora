#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <velora/globals.hpp>
#include <velora/resources/ResourceIndex.hpp>
#include <velora/resources/ResourceProvider.hpp>

// The .vpak package container (R08.3): one file holding a resource index and the payloads it refers
// to, read on demand.
//
//   Header     magic "VPAK", major.minor, flags, where the metadata and the payload area are, and
//              the CRC-32 of the metadata and of the header itself
//   Metadata   manifest (name, version, default language), the package's .vpri index, and a
//              directory entry per payload: path, compression, offset, stored and original size,
//              CRC-32 of the stored bytes
//   Payloads   the stored bytes, concatenated
//
// Opening reads and verifies the header and the metadata only. A payload is read when a resource
// resolves to it, verified against its own directory checksum, decompressed if flagged, and kept in
// a bounded cache; a package is never unpacked wholesale. A damaged payload reads as a miss.
//
// Compression is a per-entry flag, so a package can hold stored images next to compressed text. Version 1
// defines stored (0) and raw deflate (1); any other value refuses the package.

namespace velora
{

constexpr u16 kVpakMajor = 1;
constexpr u16 kVpakMinor = 0;

constexpr u64 kVpakMaxMetadataBytes = 64ull * 1024ull * 1024ull;
constexpr u64 kVpakMaxPayloadBytes = 1ull << 32; // 4 GiB per entry, after decompression
constexpr u32 kVpakMaxEntries = 1u << 20;
// Decompressed payloads kept in memory per package; older ones are dropped first.
constexpr usize kVpakCacheBytes = 16u * 1024u * 1024u;

enum class PakCompression : u8
{
    kStored = 0,
    kDeflate = 1,
};

struct PakEntry
{
    std::string path; // package-relative, normalized
    PakCompression compression = PakCompression::kStored;
    u64 offset = 0;     // from the start of the payload area
    u64 storedSize = 0; // bytes in the file
    u64 size = 0;       // bytes after decompression
    u32 checksum = 0;   // CRC-32 of the stored bytes
};

struct PakManifest
{
    std::string name;
    std::string version;
    std::string defaultLanguage;
};

struct VpakError
{
    std::string message;
};

// Builds a package in memory (velorac `pack`, tests).
class PackageWriter
{
public:
    explicit PackageWriter(PakManifest manifest) : m_manifest(std::move(manifest)) {}

    // Adds a payload. `compress` stores it deflated unless that would not make it smaller. False
    // for a path that is not a normalized resource path or that is already in the package.
    bool add(std::string path, std::span<const u8> bytes, bool compress = false);
    // The package's resource index; file candidates must name payloads of this package.
    void setIndex(ResourceIndex index)
    {
        m_index = std::move(index);
    }
    // The finished container, or the reason the package is inconsistent (an index candidate naming a
    // payload the package does not have, an invalid index). Deterministic for the same inputs.
    [[nodiscard]] std::variant<std::vector<u8>, VpakError> finish() const;

private:
    struct Pending
    {
        PakEntry entry;
        std::vector<u8> stored;
    };
    PakManifest m_manifest;
    ResourceIndex m_index;
    std::vector<Pending> m_entries;
    std::map<std::string, usize, std::less<>> m_byPath;
};

// A package opened for reading.
class PackageFile final
{
public:
    // Opens and verifies a package file. Bad magic, an unsupported version, a metadata or header
    // checksum mismatch, a directory entry outside the payload area, an unknown compression flag, a
    // damaged index or an index candidate naming a missing payload is an error.
    [[nodiscard]] static std::variant<std::shared_ptr<PackageFile>, VpakError>
    open(const std::filesystem::path& path);
    // Open a packaged resource from a compiled-in byte span. Bytes are copied once and owned.
    [[nodiscard]] static std::variant<std::shared_ptr<PackageFile>, VpakError>
    openMemory(std::span<const u8> bytes);

    [[nodiscard]] const PakManifest& manifest() const noexcept
    {
        return m_manifest;
    }
    [[nodiscard]] const ResourceIndex& index() const noexcept
    {
        return m_index;
    }
    [[nodiscard]] const std::vector<PakEntry>& entries() const noexcept
    {
        return m_entries;
    }
    [[nodiscard]] const PakEntry* find(std::string_view path) const;

    // The payload at `path`, verified and decompressed; nullopt when there is no such entry or its
    // stored bytes do not match the directory (a damaged package reads as a miss, never as data).
    [[nodiscard]] std::optional<std::vector<u8>> read(std::string_view path) const;
    [[nodiscard]] bool contains(std::string_view path) const
    {
        return find(path) != nullptr;
    }
    // Reads and checks every entry without caching them; one message per damaged entry. The
    // `velorac verify-pack` diagnostic.
    [[nodiscard]] std::vector<std::string> verify() const;
    [[nodiscard]] std::string dump() const;

    // Payload reads that went to the file (cache misses), for tests of on-demand loading.
    [[nodiscard]] u64 fileReads() const noexcept;

private:
    PackageFile() = default;
    [[nodiscard]] static std::variant<std::shared_ptr<PackageFile>, VpakError>
    parse(const std::shared_ptr<PackageFile>& package, std::istream& input, std::string_view label);
    std::optional<std::vector<u8>> load(const PakEntry& entry) const;

    std::filesystem::path m_path;
    PakManifest m_manifest;
    ResourceIndex m_index;
    std::vector<PakEntry> m_entries;
    std::map<std::string, usize, std::less<>> m_byPath;
    u64 m_payloadOffset = 0;
    u64 m_payloadBytes = 0;

    mutable std::mutex m_mutex;
    mutable std::ifstream m_stream;
    std::unique_ptr<std::istringstream> m_embeddedStream;
    mutable std::istream* m_reader = nullptr;
    mutable std::map<std::string, std::shared_ptr<const std::vector<u8>>, std::less<>> m_cache;
    mutable std::vector<std::string> m_cacheOrder; // oldest first
    mutable usize m_cacheBytes = 0;
    mutable u64 m_fileReads = 0;
};

// A provider over one package (R08.1). Its index resolves logical paths to the package's own
// payloads only, so one package can never return another package's files.
class PackageFileProvider final : public ResourceProvider
{
public:
    explicit PackageFileProvider(std::shared_ptr<const PackageFile> package) : m_package(std::move(package))
    {
    }

    std::optional<std::vector<u8>> read(const std::string& path) const override;
    bool contains(const std::string& path) const override;
    const ResourceIndex* index() const override
    {
        return &m_package->index();
    }
    std::string name() const override
    {
        return m_package->manifest().name;
    }

private:
    std::shared_ptr<const PackageFile> m_package;
};

} // namespace velora
