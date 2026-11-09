#pragma once

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <velora/globals.hpp>
#include <velora/resources/ResourceContext.hpp>
#include <velora/resources/ResourceQualifier.hpp>
#include <velora/resources/ResourceUri.hpp>

// The .vpri resource index (R08.2): every resource a package publishes, with the qualifiers of each
// variant, so a lookup resolves without touching the file system.
//
// A package carries one index and the blobs it references. The index is what makes resource lookup
// deterministic and inspectable: `velorac dump-pri` prints every candidate and the rank it gets in a
// given context, which is how a wrong-looking resource is diagnosed.
//
// The file is versioned and checksummed like .vxbf: magic "VPRI", major.minor, flags, CRC-32 over
// everything after the header, and a section directory. Sections:
//   Meta          resource map name, default language
//   Strings       the string pool every other section refers to by index
//   Blobs         string and binary payloads referenced by index (see CandidateKind)
//   Candidates    logical path + qualifier + payload or path
//
// A CandidateKind::kFile entry stores a path relative to the package root; the bytes come from a
// provider (a loose file on disk or a .vpak entry). kString and kBlob entries carry their payload in
// the index itself, which is how a .vpak's index works: the container has no file system, so `blob`
// indexes the Blobs section.

namespace velora
{

constexpr u16 kVpriMajor = 1;
constexpr u16 kVpriMinor = 0;

// Quotas (C-4.5): a file edit that makes an index absurd is refused before anything is allocated.
constexpr usize kVpriMaxBytes = 64u * 1024u * 1024u;
constexpr u32 kVpriMaxCandidates = 1u << 20;
constexpr u32 kVpriMaxStrings = 1u << 20;
constexpr u32 kVpriMaxBlobs = 1u << 20;

enum class CandidateKind : u8
{
    kFile = 0,   // path in `path`, either a loose file under the package root or a .vpak entry
    kString = 1, // text payload in blob
    kBlob = 2,   // binary payload in blob (embedded file)
};

// One variant of one logical resource.
struct ResourceCandidate
{
    CandidateKind kind = CandidateKind::kFile;
    // For kFile: the package-relative path ("Assets/Logo.scale-200.png"). Empty for embedded kinds,
    // which the index keyed by logical name resolves to directly.
    std::string path;
    // Index into the index's blob storage (kString, kBlob). Ignored for kFile.
    u32 blob = 0;
    ResourceQualifier qualifier;
};

struct ResourceIndex
{
    std::string name;            // resource map name, e.g. "Velora.Fluent"
    std::string defaultLanguage; // the tag candidates with no language qualifier are written in
    // Logical path -> its variants. The map keeps the index deterministic when written.
    std::map<std::string, std::vector<ResourceCandidate>, std::less<>> candidates;
    // Payloads for kString and kBlob candidates; index 0 is always empty so that a zero `blob` in a
    // hand-written entry cannot alias a real payload.
    std::vector<std::string> blobs;

    // Appends or replaces a file candidate. A duplicate (same path, same qualifier) is a package
    // defect and is rejected by validate().
    void addFile(std::string logicalPath, std::string packagePath, ResourceQualifier qualifier = {});
    // Appends or replaces a string candidate ("Strings/Greeting" in en-US), returning the blob index.
    u32 addString(std::string logicalPath, std::string text, ResourceQualifier qualifier = {});
    // Appends or replaces an embedded binary candidate (a compiled .vxbf inside a .vpak).
    u32 addBlob(std::string logicalPath, std::span<const u8> bytes, ResourceQualifier qualifier = {});

    [[nodiscard]] bool empty() const noexcept
    {
        return candidates.empty();
    }
    // The variants of `logicalPath`, or null.
    [[nodiscard]] const std::vector<ResourceCandidate>* variants(std::string_view logicalPath) const;
    // The best variant for `context` (highest qualifierScore), or null when the path is unknown or
    // no variant can serve the context. The index is total, so the choice is deterministic.
    [[nodiscard]] const ResourceCandidate* resolve(std::string_view logicalPath,
                                                   const ResourceContext& context) const;
    // The same, reporting the rank of the chosen candidate (0 when none).
    [[nodiscard]] const ResourceCandidate* resolve(std::string_view logicalPath,
                                                   const ResourceContext& context, u64& score) const;
    // The payload of a kString/kBlob candidate.
    [[nodiscard]] std::string_view blobOf(const ResourceCandidate& candidate) const;

    // Every logical path with a string candidate; a .resw entry's logical path is
    // "<resw stem>/<key>" ("Resources/Greeting.Text").
    [[nodiscard]] std::vector<std::string> stringKeys() const;

    // Structural check: unique (path, qualifier) pairs, blob indices in range, non-empty paths,
    // a non-empty default language. Returns an empty string when the index is valid.
    [[nodiscard]] std::string validate() const;
};

struct VpriError
{
    std::string message;
};

// Serializes an index. The writer does not validate; callers building from input run
// ResourceIndex::validate() first, and readVpri() rejects an invalid index either way.
[[nodiscard]] std::vector<u8> writeVpri(const ResourceIndex& index);

// Parses and verifies an index. A corrupt, truncated or foreign file is an error, never a
// partially-populated index.
[[nodiscard]] std::variant<ResourceIndex, VpriError> readVpri(std::span<const u8> bytes);

// A human-readable listing: every logical path, and for each variant its qualifier, kind and
// payload/path. `context`, when given, marks the candidate that would win there — the diagnostic
// `velorac dump-pri` prints.
[[nodiscard]] std::string dumpVpri(const ResourceIndex& index, const ResourceContext* context = nullptr);

} // namespace velora
