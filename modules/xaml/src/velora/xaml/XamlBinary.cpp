#include <array>
#include <cstring>
#include <format>
#include <velora/memory/CheckSum.hpp>
#include <velora/xaml/XamlBinary.hpp>

namespace velora
{

namespace
{
enum class Section : u32
{
    kMeta = 1,
    kStrings = 2,
    kTypes = 3,
    kProperties = 4,
    kValues = 5,
    kInstructions = 6,
};
constexpr usize kSectionCount = 6;
constexpr usize kHeaderBytes = 4 + 2 + 2 + 4 + 8 + 4 + 4;
constexpr usize kDirectoryEntryBytes = 12;

enum class ValueTag : u8
{
    kUnset = 0,
    kBool = 1,
    kInt = 2,
    kDouble = 3,
    kString = 4,
    kThickness = 5,
    kGridLengths = 6,
};

// ---- writing --------------------------------------------------------------------------------

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
    void u64v(u64 v)
    {
        for (int i = 0; i < 8; ++i)
            m_bytes.push_back(static_cast<u8>(v >> (8 * i)));
    }
    void f64v(f64 v)
    {
        u64 bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u64v(bits);
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

void writeValue(Writer& w, const PropertyValue& v)
{
    if (v.isBool())
    {
        w.u8v(static_cast<u8>(ValueTag::kBool));
        w.u8v(v.asBool() ? 1 : 0);
    }
    else if (v.isInt())
    {
        w.u8v(static_cast<u8>(ValueTag::kInt));
        w.u64v(static_cast<u64>(v.asInt()));
    }
    else if (v.isDouble())
    {
        w.u8v(static_cast<u8>(ValueTag::kDouble));
        w.f64v(v.asDouble());
    }
    else if (v.isString())
    {
        w.u8v(static_cast<u8>(ValueTag::kString));
        w.text(v.asString());
    }
    else if (v.isThickness())
    {
        const auto& t = v.asThickness();
        w.u8v(static_cast<u8>(ValueTag::kThickness));
        for (const f64 x : {t.left, t.top, t.right, t.bottom})
            w.f64v(x);
    }
    else if (v.isGridLengths())
    {
        w.u8v(static_cast<u8>(ValueTag::kGridLengths));
        w.u32v(static_cast<u32>(v.asGridLengths().size()));
        for (const auto& g : v.asGridLengths())
        {
            w.u8v(static_cast<u8>(g.unit));
            w.f64v(g.value);
        }
    }
    else
    {
        w.u8v(static_cast<u8>(ValueTag::kUnset)); // objects and element references never appear in literals
    }
}

std::vector<u8> section(const XamlIR& ir, Section kind)
{
    Writer w;
    switch (kind)
    {
    case Section::kMeta:
        w.text(ir.sourceName);
        w.text(ir.className);
        break;
    case Section::kStrings:
        w.u32v(static_cast<u32>(ir.strings.size()));
        for (const auto& s : ir.strings)
            w.text(s);
        break;
    case Section::kTypes:
        w.u32v(static_cast<u32>(ir.types.size()));
        for (const auto& t : ir.types)
        {
            w.u64v(t.id.value.high);
            w.u64v(t.id.value.low);
            w.text(t.className);
        }
        break;
    case Section::kProperties:
        w.u32v(static_cast<u32>(ir.properties.size()));
        for (const auto& p : ir.properties)
        {
            w.u64v(p.id);
            w.text(p.name);
        }
        break;
    case Section::kValues:
        w.u32v(static_cast<u32>(ir.values.size()));
        for (const auto& v : ir.values)
            writeValue(w, v);
        break;
    case Section::kInstructions:
        w.u32v(static_cast<u32>(ir.instructions.size()));
        for (const auto& in : ir.instructions)
        {
            w.u8v(static_cast<u8>(in.op));
            w.u32v(in.a);
            w.u32v(in.b);
            w.u32v(in.c);
            w.u32v(in.line);
            w.u32v(in.column);
        }
        break;
    }
    return std::move(w.bytes());
}

// ---- reading --------------------------------------------------------------------------------

class Reader
{
public:
    Reader(std::span<const u8> bytes, std::string context) : m_bytes(bytes), m_context(std::move(context)) {}

    bool ok() const noexcept
    {
        return m_error.empty();
    }
    const std::string& error() const noexcept
    {
        return m_error;
    }
    bool atEnd() const noexcept
    {
        return m_at == m_bytes.size();
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
        if (!take(1))
            return 0;
        return m_bytes[m_at++];
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
    u64 u64v()
    {
        if (!take(8))
            return 0;
        u64 v = 0;
        for (int i = 0; i < 8; ++i)
            v |= u64{m_bytes[m_at++]} << (8 * i);
        return v;
    }
    f64 f64v()
    {
        const u64 bits = u64v();
        f64 v = 0;
        std::memcpy(&v, &bits, sizeof v);
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
    u32 count(usize minBytes)
    {
        const u32 n = u32v();
        if (ok() && (n > kVxbfMaxRecords || static_cast<u64>(n) * minBytes > m_bytes.size() - m_at))
            fail("record count exceeds the section");
        return ok() ? n : 0;
    }

private:
    std::span<const u8> m_bytes;
    usize m_at = 0;
    std::string m_context;
    std::string m_error;
};

PropertyValue readValue(Reader& r)
{
    switch (static_cast<ValueTag>(r.u8v()))
    {
    case ValueTag::kUnset:
        return {};
    case ValueTag::kBool:
    {
        const u8 b = r.u8v();
        if (b > 1)
            r.fail("invalid boolean");
        return PropertyValue(b != 0);
    }
    case ValueTag::kInt:
        return PropertyValue(static_cast<i64>(r.u64v()));
    case ValueTag::kDouble:
        return PropertyValue(r.f64v());
    case ValueTag::kString:
        return PropertyValue(r.text());
    case ValueTag::kThickness:
    {
        Thickness t;
        t.left = r.f64v();
        t.top = r.f64v();
        t.right = r.f64v();
        t.bottom = r.f64v();
        return PropertyValue(t);
    }
    case ValueTag::kGridLengths:
    {
        const u32 n = r.count(9);
        GridLengths lengths;
        for (u32 i = 0; i < n && r.ok(); ++i)
        {
            const u8 unit = r.u8v();
            if (unit > static_cast<u8>(GridLength::Unit::kStar))
                r.fail("invalid grid unit");
            lengths.push_back({static_cast<GridLength::Unit>(unit), r.f64v()});
        }
        return PropertyValue(std::move(lengths));
    }
    }
    r.fail("unknown value tag");
    return {};
}

std::string readSection(XamlIR& ir, Section kind, std::span<const u8> bytes)
{
    Reader r(bytes, std::format("section {}", static_cast<u32>(kind)));
    switch (kind)
    {
    case Section::kMeta:
        ir.sourceName = r.text();
        ir.className = r.text();
        break;
    case Section::kStrings:
        for (u32 n = r.count(4); n-- && r.ok();)
            ir.strings.push_back(r.text());
        break;
    case Section::kTypes:
        for (u32 n = r.count(20); n-- && r.ok();)
        {
            XamlIRType t;
            t.id.value.high = r.u64v();
            t.id.value.low = r.u64v();
            t.className = r.text();
            if (r.ok() && t.id != typeIdFor(t.className))
                r.fail("type id does not match its class name");
            ir.types.push_back(std::move(t));
        }
        break;
    case Section::kProperties:
        for (u32 n = r.count(12); n-- && r.ok();)
        {
            XamlIRProperty p;
            p.id = r.u64v();
            p.name = r.text();
            ir.properties.push_back(std::move(p));
        }
        break;
    case Section::kValues:
        for (u32 n = r.count(1); n-- && r.ok();)
            ir.values.push_back(readValue(r));
        break;
    case Section::kInstructions:
        for (u32 n = r.count(21); n-- && r.ok();)
        {
            XamlIRInstruction in;
            in.op = static_cast<XamlIROp>(r.u8v());
            in.a = r.u32v();
            in.b = r.u32v();
            in.c = r.u32v();
            in.line = r.u32v();
            in.column = r.u32v();
            ir.instructions.push_back(in);
        }
        break;
    }
    if (r.ok() && !r.atEnd())
        r.fail("trailing bytes");
    return r.error();
}
} // namespace

std::vector<u8> writeVxbf(const XamlIR& ir)
{
    constexpr Section kinds[kSectionCount] = {Section::kMeta,   Section::kStrings,
                                              Section::kTypes,  Section::kProperties,
                                              Section::kValues, Section::kInstructions};
    std::vector<std::vector<u8>> bodies;
    for (const auto kind : kinds)
        bodies.push_back(section(ir, kind));

    Writer w;
    for (const char c : {'V', 'X', 'B', 'F'})
        w.u8v(static_cast<u8>(c));
    w.u16v(kVxbfMajor);
    w.u16v(kVxbfMinor);
    w.u32v(0); // flags
    w.u64v(ir.schemaFingerprint);
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

std::variant<XamlIR, VxbfError> readVxbf(std::span<const u8> bytes, u64 expectedFingerprint)
{
    const auto error = [](std::string message) { return VxbfError{std::move(message)}; };
    if (bytes.size() > kVxbfMaxBytes)
        return error("file exceeds the size quota");
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), "VXBF", 4) != 0)
        return error("not a .vxbf file (bad magic)");
    Reader header(bytes.subspan(4), "header");
    const u32 version = header.u32v();
    const u16 major = static_cast<u16>(version & 0xFFFF);
    const u16 minor = static_cast<u16>(version >> 16);
    header.u32v(); // flags: none defined in 1.0
    const u64 fingerprint = header.u64v();
    const u32 sections = header.u32v();
    const u32 checksum = header.u32v();
    if (major != kVxbfMajor)
        return error(
            std::format("unsupported version {}.{} (reader {}.{})", major, minor, kVxbfMajor, kVxbfMinor));
    if (expectedFingerprint && fingerprint != expectedFingerprint)
        return error(std::format("compiled for another element schema ({:016x}, expected {:016x})",
                                 fingerprint, expectedFingerprint));
    if (crc32(bytes.subspan(kHeaderBytes)) != checksum)
        return error("checksum mismatch (corrupted file)");
    if (sections != kSectionCount || bytes.size() < kHeaderBytes + sections * kDirectoryEntryBytes)
        return error("bad section directory");

    XamlIR ir;
    ir.schemaFingerprint = fingerprint;
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
        if (auto message = readSection(ir, static_cast<Section>(kind), bytes.subspan(offset, size));
            !message.empty())
            return error(std::move(message));
    }
    if (auto message = validate(ir); !message.empty())
        return error("invalid structure: " + message);
    return ir;
}

} // namespace velora
