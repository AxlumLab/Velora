#include <algorithm>
#include <cstring>
#include <format>
#include <velora/io/BinaryStream.hpp>
#include <velora/memory/CheckSum.hpp>
#include <velora/resources/Deflate.hpp>
#include <velora/resources/PackageFile.hpp>

namespace velora
{

namespace
{
// magic, major, minor, flags, metadata offset/size, payload offset/size, metadata CRC, header CRC
constexpr usize kHeaderBytes = 4 + 2 + 2 + 4 + 8 + 8 + 8 + 8 + 4 + 4;

using Writer = BinaryWriter;
using Reader = BinaryReader;

// Every kFile candidate must name a payload of the same package: an index cannot point outside its
// own container (no cross-package pollution, R08.5).
std::string checkIndexAgainst(const ResourceIndex& index, const auto& hasPayload)
{
    for (const auto& [logical, variants] : index.candidates)
        for (const auto& candidate : variants)
            if (candidate.kind == CandidateKind::kFile && !hasPayload(candidate.path))
                return "index candidate '" + logical + "' names '" + candidate.path +
                       "', which is not in the package";
    return {};
}
} // namespace

// ---- PackageWriter ------------------------------------------------------------------------------

bool PackageWriter::add(std::string path, std::span<const u8> bytes, bool compress)
{
    const auto normalized = normalizeResourcePath(path);
    if (!normalized || *normalized != path || m_byPath.contains(path))
        return false;
    Pending pending;
    pending.entry.path = path;
    pending.entry.size = bytes.size();
    if (compress)
    {
        auto deflated = deflateRaw(bytes);
        if (deflated.size() < bytes.size())
        {
            pending.entry.compression = PakCompression::kDeflate;
            pending.stored = std::move(deflated);
        }
    }
    if (pending.entry.compression == PakCompression::kStored)
        pending.stored.assign(bytes.begin(), bytes.end());
    pending.entry.storedSize = pending.stored.size();
    pending.entry.checksum = crc32(pending.stored);
    m_byPath.emplace(path, m_entries.size());
    m_entries.push_back(std::move(pending));
    return true;
}

std::variant<std::vector<u8>, VpakError> PackageWriter::finish() const
{
    if (auto message = m_index.validate(); !message.empty())
        return VpakError{"invalid index: " + message};
    if (auto message = checkIndexAgainst(m_index, [&](const std::string& p) { return m_byPath.contains(p); });
        !message.empty())
        return VpakError{message};

    Writer meta;
    meta.text(m_manifest.name);
    meta.text(m_manifest.version);
    meta.text(m_manifest.defaultLanguage);
    const auto index = writeVpri(m_index);
    meta.u32v(static_cast<u32>(index.size()));
    meta.raw(index);
    meta.u32v(static_cast<u32>(m_entries.size()));
    u64 offset = 0;
    for (const auto& pending : m_entries)
    {
        meta.text(pending.entry.path);
        meta.u8v(static_cast<u8>(pending.entry.compression));
        meta.u64v(offset);
        meta.u64v(pending.entry.storedSize);
        meta.u64v(pending.entry.size);
        meta.u32v(pending.entry.checksum);
        offset += pending.entry.storedSize;
    }

    const u64 metaOffset = kHeaderBytes;
    const u64 metaSize = meta.bytes().size();
    Writer w;
    for (const char c : {'V', 'P', 'A', 'K'})
        w.u8v(static_cast<u8>(c));
    w.u16v(kVpakMajor);
    w.u16v(kVpakMinor);
    w.u32v(0); // flags
    w.u64v(metaOffset);
    w.u64v(metaSize);
    w.u64v(metaOffset + metaSize);
    w.u64v(offset);
    w.u32v(crc32(meta.bytes()));
    w.u32v(crc32(w.bytes())); // the header up to here
    w.raw(meta.bytes());
    for (const auto& pending : m_entries)
        w.raw(pending.stored);
    return std::move(w.bytes());
}

// ---- PackageFile --------------------------------------------------------------------------------

std::variant<std::shared_ptr<PackageFile>, VpakError> PackageFile::open(const std::filesystem::path& path)
{
    const auto error = [&](std::string message)
    { return VpakError{path.filename().string() + ": " + std::move(message)}; };
    std::shared_ptr<PackageFile> package(new PackageFile());
    package->m_path = path;
    package->m_stream.open(path, std::ios::binary);
    if (!package->m_stream)
        return error("cannot open the package");
    package->m_reader = &package->m_stream;
    return parse(package, package->m_stream, path.filename().string());
}

std::variant<std::shared_ptr<PackageFile>, VpakError> PackageFile::openMemory(std::span<const u8> bytes)
{
    if (bytes.empty())
        return VpakError{"embedded .vpak: empty package"};
    std::shared_ptr<PackageFile> package(new PackageFile());
    std::string owned(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    package->m_embeddedStream = std::make_unique<std::istringstream>(std::move(owned), std::ios::binary);
    package->m_reader = package->m_embeddedStream.get();
    return parse(package, *package->m_reader, "embedded .vpak");
}

std::variant<std::shared_ptr<PackageFile>, VpakError>
PackageFile::parse(const std::shared_ptr<PackageFile>& package, std::istream& in, std::string_view label)
{
    const auto error = [&](std::string message)
    { return VpakError{std::string(label) + ": " + std::move(message)}; };
    in.seekg(0, std::ios::end);
    const u64 fileSize = static_cast<u64>(in.tellg());
    in.seekg(0);

    std::vector<u8> header(kHeaderBytes);
    if (fileSize < kHeaderBytes || !in.read(reinterpret_cast<char*>(header.data()), kHeaderBytes))
        return error("not a .vpak file (too short)");
    if (std::memcmp(header.data(), "VPAK", 4) != 0)
        return error("not a .vpak file (bad magic)");
    Reader h(std::span<const u8>(header).subspan(4));
    const u16 major = h.u16v(), minor = h.u16v();
    h.u32v(); // flags: none defined in 1.0
    const u64 metaOffset = h.u64v(), metaSize = h.u64v();
    const u64 payloadOffset = h.u64v(), payloadSize = h.u64v();
    const u32 metaChecksum = h.u32v(), headerChecksum = h.u32v();
    if (major != kVpakMajor)
        return error(
            std::format("unsupported version {}.{} (reader {}.{})", major, minor, kVpakMajor, kVpakMinor));
    if (crc32(std::span<const u8>(header).first(kHeaderBytes - 4)) != headerChecksum)
        return error("header checksum mismatch (corrupted package)");
    if (metaOffset != kHeaderBytes || metaSize > kVpakMaxMetadataBytes || metaSize > fileSize - metaOffset ||
        payloadOffset != metaOffset + metaSize || payloadSize > fileSize - payloadOffset)
        return error("metadata or payload area outside the file");

    std::vector<u8> meta(static_cast<usize>(metaSize));
    if (!in.read(reinterpret_cast<char*>(meta.data()), static_cast<std::streamsize>(metaSize)))
        return error("truncated metadata");
    if (crc32(meta) != metaChecksum)
        return error("metadata checksum mismatch (corrupted package)");

    Reader m(meta);
    package->m_manifest.name = m.text();
    package->m_manifest.version = m.text();
    package->m_manifest.defaultLanguage = m.text();
    const u32 indexSize = m.u32v();
    const auto indexBytes = m.raw(indexSize);
    if (!m.ok())
        return error("truncated metadata");
    auto index = readVpri(indexBytes);
    if (const auto* e = std::get_if<VpriError>(&index))
        return error("index: " + e->message);
    package->m_index = std::move(std::get<ResourceIndex>(index));

    const u32 count = m.u32v();
    // An entry takes at least 33 bytes (empty path); the count cannot claim more than that allows.
    if (!m.ok() || count > kVpakMaxEntries || static_cast<u64>(count) * 33 > m.left())
        return error("bad directory entry count");
    for (u32 i = 0; i < count; ++i)
    {
        PakEntry entry;
        entry.path = m.text();
        const u8 compression = m.u8v();
        entry.offset = m.u64v();
        entry.storedSize = m.u64v();
        entry.size = m.u64v();
        entry.checksum = m.u32v();
        if (!m.ok())
            return error("truncated directory");
        const auto normalized = normalizeResourcePath(entry.path);
        if (!normalized || *normalized != entry.path)
            return error("directory entry '" + entry.path + "' is not a normalized resource path");
        if (compression > static_cast<u8>(PakCompression::kDeflate))
            return error(std::format("entry '{}': unknown compression {}", entry.path, compression));
        entry.compression = static_cast<PakCompression>(compression);
        if (entry.offset > payloadSize || entry.storedSize > payloadSize - entry.offset)
            return error("entry '" + entry.path + "' lies outside the payload area");
        if (entry.size > kVpakMaxPayloadBytes ||
            (entry.compression == PakCompression::kStored && entry.size != entry.storedSize))
            return error("entry '" + entry.path + "' has inconsistent sizes");
        if (package->m_byPath.contains(entry.path))
            return error("duplicate directory entry '" + entry.path + "'");
        package->m_byPath.emplace(entry.path, package->m_entries.size());
        package->m_entries.push_back(std::move(entry));
    }
    if (!m.atEnd())
        return error("trailing metadata bytes");
    if (auto message = checkIndexAgainst(package->m_index,
                                         [&](const std::string& p) { return package->m_byPath.contains(p); });
        !message.empty())
        return error(message);
    package->m_payloadOffset = payloadOffset;
    package->m_payloadBytes = payloadSize;
    return package;
}

const PakEntry* PackageFile::find(std::string_view path) const
{
    const auto it = m_byPath.find(path);
    return it == m_byPath.end() ? nullptr : &m_entries[it->second];
}

std::optional<std::vector<u8>> PackageFile::load(const PakEntry& entry) const
{
    // Callers hold m_mutex: the stream position is shared.
    std::vector<u8> stored(static_cast<usize>(entry.storedSize));
    m_reader->clear();
    m_reader->seekg(static_cast<std::streamoff>(m_payloadOffset + entry.offset));
    ++m_fileReads;
    if (!m_reader->read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size())))
        return std::nullopt;
    if (crc32(stored) != entry.checksum)
        return std::nullopt;
    if (entry.compression == PakCompression::kStored)
        return stored;
    return inflateRaw(stored, entry.size);
}

std::optional<std::vector<u8>> PackageFile::read(std::string_view path) const
{
    const PakEntry* entry = find(path);
    if (!entry)
        return std::nullopt;
    std::lock_guard lock(m_mutex);
    if (const auto it = m_cache.find(path); it != m_cache.end())
        return *it->second;
    auto bytes = load(*entry);
    if (!bytes)
        return std::nullopt;
    if (bytes->size() <= kVpakCacheBytes)
    {
        while (m_cacheBytes + bytes->size() > kVpakCacheBytes && !m_cacheOrder.empty())
        {
            const auto oldest = m_cache.find(m_cacheOrder.front());
            m_cacheBytes -= oldest->second->size();
            m_cache.erase(oldest);
            m_cacheOrder.erase(m_cacheOrder.begin());
        }
        m_cache.emplace(entry->path, std::make_shared<const std::vector<u8>>(*bytes));
        m_cacheOrder.push_back(entry->path);
        m_cacheBytes += bytes->size();
    }
    return bytes;
}

std::vector<std::string> PackageFile::verify() const
{
    std::vector<std::string> problems;
    std::lock_guard lock(m_mutex);
    for (const auto& entry : m_entries)
        if (!load(entry))
            problems.push_back(
                std::format("{}: stored bytes do not match the directory ({}{} bytes, crc {:08x})",
                            entry.path, entry.compression == PakCompression::kDeflate ? "deflate, " : "",
                            entry.storedSize, entry.checksum));
    return problems;
}

u64 PackageFile::fileReads() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_fileReads;
}

std::string PackageFile::dump() const
{
    std::string out =
        std::format("vpak {} {} (default language {}, {} entries, {} payload bytes)\n", m_manifest.name,
                    m_manifest.version, m_manifest.defaultLanguage, m_entries.size(), m_payloadBytes);
    for (const auto& entry : m_entries)
        out += std::format("  {:<44} {:<8} {:>10} -> {:>10}  crc {:08x}\n", entry.path,
                           entry.compression == PakCompression::kDeflate ? "deflate" : "stored",
                           entry.storedSize, entry.size, entry.checksum);
    return out;
}

// ---- PackageFileProvider ------------------------------------------------------------------------

std::optional<std::vector<u8>> PackageFileProvider::read(const std::string& path) const
{
    return m_package->read(path);
}

bool PackageFileProvider::contains(const std::string& path) const
{
    return m_package->contains(path);
}

} // namespace velora
