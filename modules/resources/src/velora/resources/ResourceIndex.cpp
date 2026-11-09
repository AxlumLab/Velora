#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <velora/memory/CheckSum.hpp>
#include <velora/resources/ResourceIndex.hpp>

namespace velora
{

namespace
{
// ---- writing ----------------------------------------------------------------------------------

class Writer
{
public:
    void u8v(u8 v)
    {
        m_bytes.push_back(v);
    }
    void u16v(u16 v)
    {
        for (int i = 0; i < 2; ++i)
            m_bytes.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void u32v(u32 v)
    {
        for (int i = 0; i < 4; ++i)
            m_bytes.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void text(std::string_view s)
    {
        u32v(static_cast<u32>(s.size()));
        m_bytes.insert(m_bytes.end(), s.begin(), s.end());
    }
    std::vector<u8>& bytes() noexcept
    {
        return m_bytes;
    }

private:
    std::vector<u8> m_bytes;
};

enum class Section : u32
{
    kMeta = 1,
    kStrings = 2,
    kBlobs = 3,
    kCandidates = 4,
};

constexpr usize kSectionCount = 4;
constexpr usize kHeaderBytes = 4 + 2 + 2 + 4 + 4 + 4;
constexpr usize kDirectoryEntryBytes = 12;

// The string pool and the logical paths share one table: a logical path is written once and every
// candidate of it references it by index, which keeps an index with many variants compact.
class StringPool
{
public:
    u32 intern(std::string_view text)
    {
        const auto it = m_index.find(text);
        if (it != m_index.end())
            return it->second;
        const u32 id = static_cast<u32>(m_strings.size());
        m_strings.emplace_back(text);
        m_index.emplace(m_strings.back(), id);
        return id;
    }
    const std::vector<std::string>& strings() const noexcept
    {
        return m_strings;
    }

private:
    std::vector<std::string> m_strings;
    std::map<std::string, u32, std::less<>> m_index;
};

std::vector<u8> writeSections(const ResourceIndex& index)
{
    StringPool pool;
    for (const auto& [path, variants] : index.candidates)
    {
        pool.intern(path);
        for (const auto& variant : variants)
        {
            pool.intern(variant.path);
            pool.intern(qualifierString(variant.qualifier));
        }
    }

    Writer meta;
    meta.text(index.name);
    meta.text(index.defaultLanguage);

    Writer strings;
    strings.u32v(static_cast<u32>(pool.strings().size()));
    for (const auto& s : pool.strings())
        strings.text(s);

    Writer blobs;
    blobs.u32v(static_cast<u32>(index.blobs.size()));
    for (const auto& b : index.blobs)
        blobs.text(b);

    Writer candidates;
    u32 total = 0;
    for (const auto& [path, variants] : index.candidates)
        total += static_cast<u32>(variants.size());
    candidates.u32v(total);
    for (const auto& [path, variants] : index.candidates)
        for (const auto& variant : variants)
        {
            candidates.u32v(pool.intern(path)); // the lookup interned it already
            candidates.u8v(static_cast<u8>(variant.kind));
            candidates.u32v(pool.intern(variant.path));
            candidates.u32v(pool.intern(qualifierString(variant.qualifier)));
            candidates.u32v(variant.blob);
        }

    constexpr Section kinds[kSectionCount] = {Section::kMeta, Section::kStrings, Section::kBlobs,
                                              Section::kCandidates};
    std::vector<std::vector<u8>> bodies;
    bodies.push_back(std::move(meta.bytes()));
    bodies.push_back(std::move(strings.bytes()));
    bodies.push_back(std::move(blobs.bytes()));
    bodies.push_back(std::move(candidates.bytes()));

    Writer w;
    for (const char c : {'V', 'P', 'R', 'I'})
        w.u8v(static_cast<u8>(c));
    w.u16v(kVpriMajor);
    w.u16v(kVpriMinor);
    w.u32v(0); // flags
    w.u32v(static_cast<u32>(kSectionCount));
    w.u32v(0); // checksum, patched below
    u32 offset = static_cast<u32>(kHeaderBytes + kSectionCount * kDirectoryEntryBytes);
    for (usize i = 0; i < kSectionCount; ++i)
    {
        w.u32v(static_cast<u32>(kinds[i]));
        w.u32v(offset);
        w.u32v(static_cast<u32>(bodies[i].size()));
        offset += static_cast<u32>(bodies[i].size());
    }
    auto& bytes = w.bytes();
    for (const auto& body : bodies)
        bytes.insert(bytes.end(), body.begin(), body.end());
    const u32 checksum = crc32(std::span<const u8>(bytes).subspan(kHeaderBytes));
    for (int i = 0; i < 4; ++i)
        bytes[kHeaderBytes - 4 + static_cast<usize>(i)] = static_cast<u8>(checksum >> (8 * i));
    return std::move(bytes);
}

// ---- reading ----------------------------------------------------------------------------------

class Reader
{
public:
    Reader(std::span<const u8> bytes, std::string context) : m_bytes(bytes), m_context(std::move(context)) {}

    bool ok() const noexcept
    {
        return m_error.empty();
    }
    bool atEnd() const noexcept
    {
        return m_at == m_bytes.size();
    }
    const std::string& error() const noexcept
    {
        return m_error;
    }
    void fail(std::string_view what)
    {
        if (m_error.empty())
            m_error = m_context + ": " + std::string(what);
    }
    bool take(usize n)
    {
        if (!ok() || m_bytes.size() - m_at < n)
        {
            fail("truncated");
            return false;
        }
        return true;
    }
    u8 u8v()
    {
        return take(1) ? m_bytes[m_at++] : 0;
    }
    u32 u32v()
    {
        if (!take(4))
            return 0;
        u32 v = 0;
        for (int i = 0; i < 4; ++i)
            v |= u32{m_bytes[m_at++]} << (8 * i);
        return v;
    }
    std::string text()
    {
        const u32 n = u32v();
        if (!take(n))
            return {};
        std::string s(reinterpret_cast<const char*>(m_bytes.data() + m_at), n);
        m_at += n;
        return s;
    }
    // A record count, bounded by the quota and by the bytes left (every record takes >= minBytes).
    u32 count(u32 quota, usize minBytes)
    {
        const u32 n = u32v();
        if (ok() && (n > quota || static_cast<u64>(n) * minBytes > m_bytes.size() - m_at))
            fail("record count exceeds the section");
        return ok() ? n : 0;
    }

private:
    std::span<const u8> m_bytes;
    usize m_at = 0;
    std::string m_context;
    std::string m_error;
};

std::string readSection(ResourceIndex& index, const std::vector<std::string>& strings, Section kind,
                        std::span<const u8> bytes)
{
    Reader r(bytes, std::format("section {}", static_cast<u32>(kind)));
    const auto string = [&](u32 id) -> std::optional<std::string>
    {
        if (id >= strings.size())
        {
            r.fail("string index out of range");
            return std::nullopt;
        }
        return strings[id];
    };
    switch (kind)
    {
    case Section::kMeta:
        index.name = r.text();
        index.defaultLanguage = r.text();
        break;
    case Section::kStrings:
        break; // read up front: the pool every other section refers to
    case Section::kBlobs:
    {
        const u32 n = r.count(kVpriMaxBlobs, 4);
        for (u32 i = 0; i < n && r.ok(); ++i)
            index.blobs.push_back(r.text());
        break;
    }
    case Section::kCandidates:
    {
        const u32 n = r.count(kVpriMaxCandidates, 17);
        for (u32 i = 0; i < n && r.ok(); ++i)
        {
            ResourceCandidate candidate;
            const u32 pathId = r.u32v();
            candidate.kind = static_cast<CandidateKind>(r.u8v());
            const u32 physicalId = r.u32v();
            const u32 qualifierId = r.u32v();
            candidate.blob = r.u32v();
            if (!r.ok())
                break;
            const auto key = string(pathId), physical = string(physicalId), qualifier = string(qualifierId);
            if (!key || !physical || !qualifier)
                break;
            if (candidate.kind != CandidateKind::kFile && candidate.kind != CandidateKind::kString &&
                candidate.kind != CandidateKind::kBlob)
            {
                r.fail("unknown candidate kind");
                break;
            }
            const auto parsed = parseQualifiers(*qualifier);
            if (!parsed)
            {
                r.fail("malformed qualifier '" + *qualifier + "'");
                break;
            }
            candidate.path = *physical;
            candidate.qualifier = *parsed;
            index.candidates[*key].push_back(std::move(candidate));
        }
        break;
    }
    }
    if (r.ok() && !r.atEnd())
        r.fail("trailing bytes");
    return r.error();
}
} // namespace

// ---- ResourceIndex ----------------------------------------------------------------------------

void ResourceIndex::addFile(std::string logicalPath, std::string packagePath, ResourceQualifier qualifier)
{
    candidates[logicalPath].push_back(
        {CandidateKind::kFile, std::move(packagePath), 0, std::move(qualifier)});
}

u32 ResourceIndex::addString(std::string logicalPath, std::string text, ResourceQualifier qualifier)
{
    if (blobs.empty())
        blobs.emplace_back(); // index 0 is reserved (see ResourceIndex::blobs)
    blobs.push_back(std::move(text));
    const u32 blob = static_cast<u32>(blobs.size() - 1);
    candidates[logicalPath].push_back({CandidateKind::kString, {}, blob, std::move(qualifier)});
    return blob;
}

u32 ResourceIndex::addBlob(std::string logicalPath, std::span<const u8> bytes, ResourceQualifier qualifier)
{
    if (blobs.empty())
        blobs.emplace_back();
    blobs.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const u32 blob = static_cast<u32>(blobs.size() - 1);
    candidates[logicalPath].push_back({CandidateKind::kBlob, {}, blob, std::move(qualifier)});
    return blob;
}

const std::vector<ResourceCandidate>* ResourceIndex::variants(std::string_view logicalPath) const
{
    const auto it = candidates.find(logicalPath);
    return it == candidates.end() ? nullptr : &it->second;
}

const ResourceCandidate* ResourceIndex::resolve(std::string_view logicalPath, const ResourceContext& context,
                                                u64& score) const
{
    score = 0;
    const auto* all = variants(logicalPath);
    if (!all)
        return nullptr;
    const ResourceCandidate* best = nullptr;
    for (const auto& candidate : *all)
    {
        const auto rank = qualifierScore(candidate.qualifier, context);
        if (rank && (!best || *rank > score))
        {
            best = &candidate;
            score = *rank;
        }
    }
    return best;
}

const ResourceCandidate* ResourceIndex::resolve(std::string_view logicalPath,
                                                const ResourceContext& context) const
{
    u64 score = 0;
    return resolve(logicalPath, context, score);
}

std::string_view ResourceIndex::blobOf(const ResourceCandidate& candidate) const
{
    if (candidate.kind == CandidateKind::kFile || candidate.blob >= blobs.size())
        return {};
    return blobs[candidate.blob];
}

std::vector<std::string> ResourceIndex::stringKeys() const
{
    std::vector<std::string> keys;
    for (const auto& [path, variants] : candidates)
        if (std::ranges::any_of(variants,
                                [](const ResourceCandidate& c) { return c.kind == CandidateKind::kString; }))
            keys.push_back(path);
    return keys;
}

std::string ResourceIndex::validate() const
{
    if (name.empty())
        return "the index has no name";
    if (defaultLanguage.empty())
        return "the index has no default language";
    if (candidates.size() > kVpriMaxCandidates)
        return "too many candidates";
    for (const auto& [path, variants] : candidates)
    {
        if (!normalizeResourcePath(path) || *normalizeResourcePath(path) != path)
            return "logical path '" + path + "' is not a normalized resource path";
        if (variants.empty())
            return "logical path '" + path + "' has no candidates";
        for (std::size_t i = 0; i < variants.size(); ++i)
        {
            const auto& candidate = variants[i];
            if (candidate.kind == CandidateKind::kFile)
            {
                if (candidate.path.empty())
                    return "file candidate '" + path + "' has no path";
                const auto normalized = normalizeResourcePath(candidate.path);
                if (!normalized || *normalized != candidate.path)
                    return "candidate path '" + candidate.path + "' is not a normalized resource path";
            }
            else if (candidate.blob == 0 || candidate.blob >= blobs.size())
            {
                return "payload candidate '" + path + "' refers to no payload";
            }
            // Two candidates that tie on every ranked axis make resolution arbitrary; the writer
            // cannot order them and the reader must not pick one at random.
            for (std::size_t j = i + 1; j < variants.size(); ++j)
                if (variants[j].kind == candidate.kind && variants[j].qualifier == candidate.qualifier &&
                    variants[j].blob == candidate.blob && variants[j].path == candidate.path)
                    return "duplicate candidate '" + path + "' with qualifier '" +
                           qualifierString(candidate.qualifier) + "'";
        }
    }
    return {};
}

std::vector<u8> writeVpri(const ResourceIndex& index)
{
    return writeSections(index);
}

std::variant<ResourceIndex, VpriError> readVpri(std::span<const u8> bytes)
{
    const auto error = [](std::string message) { return VpriError{std::move(message)}; };
    if (bytes.size() > kVpriMaxBytes)
        return error("index exceeds the size quota");
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), "VPRI", 4) != 0)
        return error("not a .vpri file (bad magic)");
    Reader header(bytes.subspan(4), "header");
    const u32 version = header.u32v();
    const u16 major = static_cast<u16>(version & 0xFFFF);
    const u16 minor = static_cast<u16>(version >> 16);
    header.u32v(); // flags: none defined in 1.0
    const u32 sections = header.u32v();
    const u32 checksum = header.u32v();
    if (major != kVpriMajor)
        return error(
            std::format("unsupported version {}.{} (reader {}.{})", major, minor, kVpriMajor, kVpriMinor));
    if (crc32(bytes.subspan(kHeaderBytes)) != checksum)
        return error("checksum mismatch (corrupted index)");
    if (sections != kSectionCount || bytes.size() < kHeaderBytes + sections * kDirectoryEntryBytes)
        return error("bad section directory");

    // The string pool is read first: every other section references it by index, so a corrupted
    // pool is reported as such instead of as an out-of-range reference somewhere else.
    std::vector<std::string> strings;
    std::vector<std::pair<Section, std::span<const u8>>> bodies;
    Reader directory(bytes.subspan(kHeaderBytes, sections * kDirectoryEntryBytes), "directory");
    bool seen[kSectionCount + 1] = {};
    for (u32 i = 0; i < sections; ++i)
    {
        const u32 kind = directory.u32v(), offset = directory.u32v(), size = directory.u32v();
        if (kind == 0 || kind > kSectionCount || seen[kind])
            return error("bad or repeated section kind");
        seen[kind] = true;
        if (offset < kHeaderBytes || offset > bytes.size() || size > bytes.size() - offset)
            return error("section outside the file");
        bodies.emplace_back(static_cast<Section>(kind), bytes.subspan(offset, size));
    }
    for (const auto& [kind, body] : bodies)
    {
        if (kind != Section::kStrings)
            continue;
        Reader r(body, "section 2");
        const u32 n = r.count(kVpriMaxStrings, 4);
        for (u32 i = 0; i < n && r.ok(); ++i)
            strings.push_back(r.text());
        if (!r.ok())
            return error(r.error());
        if (!r.atEnd())
            return error("section 2: trailing bytes");
    }

    ResourceIndex index;
    // Blobs and candidates are read in section order; Meta first so a failure names the map.
    for (const auto kind : {Section::kMeta, Section::kBlobs, Section::kCandidates})
        for (const auto& [sectionKind, body] : bodies)
            if (sectionKind == kind)
                if (auto message = readSection(index, strings, kind, body); !message.empty())
                    return error(std::move(message));
    if (auto message = index.validate(); !message.empty())
        return error("invalid index: " + message);
    return index;
}

std::string dumpVpri(const ResourceIndex& index, const ResourceContext* context)
{
    std::string out =
        std::format("vpri {} ({} logical paths, {} payloads, default language {})\n",
                    index.name.empty() ? "<unnamed>" : index.name, index.candidates.size(),
                    index.blobs.size(), index.defaultLanguage.empty() ? "<none>" : index.defaultLanguage);
    for (const auto& [path, variants] : index.candidates)
    {
        const ResourceCandidate* winner = context ? index.resolve(path, *context) : nullptr;
        out += std::format("  {}\n", path);
        for (const auto& candidate : variants)
        {
            const std::string qualifier = qualifierString(candidate.qualifier);
            const std::string where =
                candidate.kind == CandidateKind::kFile
                    ? candidate.path
                    : std::format("{} ({} bytes)",
                                  candidate.kind == CandidateKind::kString ? "string" : "blob",
                                  index.blobOf(candidate).size());
            out += std::format("    {:<8} {:<28} {}{}\n",
                               candidate.kind == CandidateKind::kFile ? "file" : "embed",
                               qualifier.empty() ? "(neutral)" : qualifier, where,
                               &candidate == winner ? "   <- resolved" : "");
        }
    }
    return out;
}

} // namespace velora
