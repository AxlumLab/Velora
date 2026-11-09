#include <algorithm>
#include <array>
#include <velora/resources/Deflate.hpp>

namespace velora
{

namespace
{
constexpr std::array<u16, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<u8, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<u16, 30> kDistanceBase = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                               33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                               1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<u8, 30> kDistanceExtra = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// The order code-length code lengths are transmitted in (RFC 1951 3.2.7).
constexpr std::array<u8, 19> kCodeLengthOrder = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                 11, 4,  12, 3, 13, 2, 14, 1, 15};

constexpr u32 kWindow = 32768;
constexpr u32 kMinMatch = 3;
constexpr u32 kMaxMatch = 258;

// ---- encoding -----------------------------------------------------------------------------------

class BitWriter
{
public:
    // Writes `count` bits of `value`, least significant first (deflate's bit order for data).
    void bits(u32 value, u32 count)
    {
        for (u32 i = 0; i < count; ++i)
            bit((value >> i) & 1);
    }
    // Writes a Huffman code, most significant bit first.
    void code(u32 value, u32 length)
    {
        for (u32 i = length; i-- > 0;)
            bit((value >> i) & 1);
    }
    std::vector<u8> finish()
    {
        if (m_count)
            m_bytes.push_back(m_current);
        return std::move(m_bytes);
    }

private:
    void bit(u32 b)
    {
        m_current |= static_cast<u8>(b << m_count);
        if (++m_count == 8)
        {
            m_bytes.push_back(m_current);
            m_current = 0;
            m_count = 0;
        }
    }
    std::vector<u8> m_bytes;
    u8 m_current = 0;
    u32 m_count = 0;
};

// Fixed literal/length code (RFC 1951 3.2.6).
void writeLiteral(BitWriter& w, u32 symbol)
{
    if (symbol < 144)
        w.code(0x30 + symbol, 8);
    else if (symbol < 256)
        w.code(0x190 + (symbol - 144), 9);
    else if (symbol < 280)
        w.code(symbol - 256, 7);
    else
        w.code(0xC0 + (symbol - 280), 8);
}

void writeMatch(BitWriter& w, u32 length, u32 distance)
{
    u32 l = 0;
    while (l + 1 < kLengthBase.size() && kLengthBase[l + 1] <= length)
        ++l;
    writeLiteral(w, 257 + l);
    w.bits(length - kLengthBase[l], kLengthExtra[l]);
    u32 d = 0;
    while (d + 1 < kDistanceBase.size() && kDistanceBase[d + 1] <= distance)
        ++d;
    w.code(d, 5);
    w.bits(distance - kDistanceBase[d], kDistanceExtra[d]);
}

// ---- decoding -----------------------------------------------------------------------------------

class BitReader
{
public:
    explicit BitReader(std::span<const u8> bytes) : m_bytes(bytes) {}

    // Reads `count` bits, least significant first; false at the end of the input.
    bool bits(u32 count, u32& value)
    {
        value = 0;
        for (u32 i = 0; i < count; ++i)
        {
            if ((m_bit >> 3) >= m_bytes.size())
                return false;
            value |= static_cast<u32>((m_bytes[m_bit >> 3] >> (m_bit & 7)) & 1) << i;
            ++m_bit;
        }
        return true;
    }
    void alignToByte() noexcept
    {
        m_bit = (m_bit + 7) & ~usize{7};
    }

private:
    std::span<const u8> m_bytes;
    usize m_bit = 0;
};

// A canonical Huffman decoder (RFC 1951 3.2.2): per code length, the first code and the index of
// its first symbol in the symbol table sorted by (length, symbol).
class Huffman
{
public:
    // False for an over-subscribed code. An incomplete code is accepted (RFC 1951 allows a single
    // distance code), and decoding an unassigned code fails.
    bool build(std::span<const u8> lengths)
    {
        m_count.fill(0);
        m_symbols.clear();
        for (const u8 length : lengths)
            ++m_count[length];
        m_count[0] = 0;
        i32 left = 1;
        for (u32 bits = 1; bits <= 15; ++bits)
        {
            left = left * 2 - static_cast<i32>(m_count[bits]);
            if (left < 0)
                return false;
        }
        std::array<u16, 16> offset{};
        for (u32 bits = 1; bits < 15; ++bits)
            offset[bits + 1] = static_cast<u16>(offset[bits] + m_count[bits]);
        m_symbols.resize(lengths.size());
        for (u32 symbol = 0; symbol < lengths.size(); ++symbol)
            if (lengths[symbol])
                m_symbols[offset[lengths[symbol]]++] = static_cast<u16>(symbol);
        return true;
    }

    bool decode(BitReader& reader, u32& symbol) const
    {
        i32 code = 0, first = 0, index = 0;
        for (u32 bits = 1; bits <= 15; ++bits)
        {
            u32 bit = 0;
            if (!reader.bits(1, bit))
                return false;
            code |= static_cast<i32>(bit);
            const i32 count = m_count[bits];
            if (code - count < first)
            {
                symbol = m_symbols[static_cast<usize>(index + (code - first))];
                return true;
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }

private:
    std::array<u16, 16> m_count{};
    std::vector<u16> m_symbols;
};

bool decodeCodes(BitReader& reader, const Huffman& literal, const Huffman& distance, std::vector<u8>& out,
                 u64 limit)
{
    for (;;)
    {
        u32 symbol = 0;
        if (!literal.decode(reader, symbol))
            return false;
        if (symbol < 256)
        {
            if (out.size() >= limit)
                return false;
            out.push_back(static_cast<u8>(symbol));
            continue;
        }
        if (symbol == 256)
            return true;
        symbol -= 257;
        if (symbol >= kLengthBase.size())
            return false;
        u32 extra = 0;
        if (!reader.bits(kLengthExtra[symbol], extra))
            return false;
        const u32 length = kLengthBase[symbol] + extra;
        u32 d = 0;
        if (!distance.decode(reader, d) || d >= kDistanceBase.size())
            return false;
        if (!reader.bits(kDistanceExtra[d], extra))
            return false;
        const usize back = kDistanceBase[d] + extra;
        if (back > out.size() || out.size() + length > limit)
            return false;
        for (u32 i = 0; i < length; ++i)
            out.push_back(out[out.size() - back]);
    }
}

bool readDynamicTables(BitReader& reader, Huffman& literal, Huffman& distance)
{
    u32 hlit = 0, hdist = 0, hclen = 0;
    if (!reader.bits(5, hlit) || !reader.bits(5, hdist) || !reader.bits(4, hclen))
        return false;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 30)
        return false;
    std::array<u8, 19> codeLengths{};
    for (u32 i = 0; i < hclen; ++i)
    {
        u32 length = 0;
        if (!reader.bits(3, length))
            return false;
        codeLengths[kCodeLengthOrder[i]] = static_cast<u8>(length);
    }
    Huffman lengthCode;
    if (!lengthCode.build(codeLengths))
        return false;
    std::vector<u8> lengths;
    lengths.reserve(hlit + hdist);
    while (lengths.size() < hlit + hdist)
    {
        u32 symbol = 0;
        if (!lengthCode.decode(reader, symbol))
            return false;
        if (symbol < 16)
        {
            lengths.push_back(static_cast<u8>(symbol));
            continue;
        }
        u32 repeat = 0;
        u8 value = 0;
        if (symbol == 16)
        {
            if (lengths.empty() || !reader.bits(2, repeat))
                return false;
            value = lengths.back();
            repeat += 3;
        }
        else if (symbol == 17)
        {
            if (!reader.bits(3, repeat))
                return false;
            repeat += 3;
        }
        else
        {
            if (!reader.bits(7, repeat))
                return false;
            repeat += 11;
        }
        if (lengths.size() + repeat > hlit + hdist)
            return false;
        lengths.insert(lengths.end(), repeat, value);
    }
    if (lengths[256] == 0)
        return false; // no end-of-block code
    return literal.build(std::span<const u8>(lengths).first(hlit)) &&
           distance.build(std::span<const u8>(lengths).subspan(hlit));
}

const Huffman& fixedLiteral()
{
    static const Huffman table = []
    {
        std::array<u8, 288> lengths{};
        std::fill(lengths.begin(), lengths.begin() + 144, u8{8});
        std::fill(lengths.begin() + 144, lengths.begin() + 256, u8{9});
        std::fill(lengths.begin() + 256, lengths.begin() + 280, u8{7});
        std::fill(lengths.begin() + 280, lengths.end(), u8{8});
        Huffman h;
        h.build(lengths);
        return h;
    }();
    return table;
}

const Huffman& fixedDistance()
{
    static const Huffman table = []
    {
        std::array<u8, 30> lengths{};
        lengths.fill(5);
        Huffman h;
        h.build(lengths);
        return h;
    }();
    return table;
}
} // namespace

std::vector<u8> deflateRaw(std::span<const u8> bytes)
{
    BitWriter w;
    w.bits(1, 1); // BFINAL: one block
    w.bits(1, 2); // BTYPE 01: fixed Huffman
    // Hash chains over 3-byte prefixes; `head` is the latest position with a hash, `previous`
    // links to the one before it inside the window.
    constexpr u32 kHashBits = 15;
    constexpr u32 kMaxChain = 64;
    std::vector<i32> head(1u << kHashBits, -1);
    std::vector<i32> previous(bytes.size(), -1);
    const auto hash = [&](usize at)
    {
        const u32 v = (u32{bytes[at]} << 16) | (u32{bytes[at + 1]} << 8) | bytes[at + 2];
        return (v * 2654435761u) >> (32 - kHashBits);
    };
    const auto insert = [&](usize at)
    {
        if (at + kMinMatch > bytes.size())
            return;
        const u32 h = hash(at);
        previous[at] = head[h];
        head[h] = static_cast<i32>(at);
    };
    usize at = 0;
    while (at < bytes.size())
    {
        u32 bestLength = 0, bestDistance = 0;
        if (at + kMinMatch <= bytes.size())
        {
            const usize limit = std::min<usize>(kMaxMatch, bytes.size() - at);
            u32 chain = 0;
            for (i32 candidate = head[hash(at)]; candidate >= 0 && chain < kMaxChain;
                 candidate = previous[static_cast<usize>(candidate)], ++chain)
            {
                const usize from = static_cast<usize>(candidate);
                if (at - from > kWindow)
                    break;
                u32 length = 0;
                while (length < limit && bytes[from + length] == bytes[at + length])
                    ++length;
                if (length > bestLength)
                {
                    bestLength = length;
                    bestDistance = static_cast<u32>(at - from);
                    if (length == limit)
                        break;
                }
            }
        }
        if (bestLength >= kMinMatch)
        {
            writeMatch(w, bestLength, bestDistance);
            for (u32 i = 0; i < bestLength; ++i)
                insert(at + i);
            at += bestLength;
        }
        else
        {
            writeLiteral(w, bytes[at]);
            insert(at);
            ++at;
        }
    }
    writeLiteral(w, 256);
    return w.finish();
}

std::optional<std::vector<u8>> inflateRaw(std::span<const u8> bytes, u64 expectedSize)
{
    BitReader reader(bytes);
    std::vector<u8> out;
    out.reserve(static_cast<usize>(std::min<u64>(expectedSize, 64u * 1024u * 1024u)));
    u32 last = 0;
    do
    {
        u32 type = 0;
        if (!reader.bits(1, last) || !reader.bits(2, type))
            return std::nullopt;
        if (type == 0)
        {
            reader.alignToByte();
            u32 length = 0, inverse = 0;
            if (!reader.bits(16, length) || !reader.bits(16, inverse) || (length ^ 0xFFFFu) != inverse)
                return std::nullopt;
            if (out.size() + length > expectedSize)
                return std::nullopt;
            for (u32 i = 0; i < length; ++i)
            {
                u32 byte = 0;
                if (!reader.bits(8, byte))
                    return std::nullopt;
                out.push_back(static_cast<u8>(byte));
            }
        }
        else if (type == 1)
        {
            if (!decodeCodes(reader, fixedLiteral(), fixedDistance(), out, expectedSize))
                return std::nullopt;
        }
        else if (type == 2)
        {
            Huffman literal, distance;
            if (!readDynamicTables(reader, literal, distance) ||
                !decodeCodes(reader, literal, distance, out, expectedSize))
                return std::nullopt;
        }
        else
        {
            return std::nullopt;
        }
    } while (!last);
    if (out.size() != expectedSize)
        return std::nullopt;
    return out;
}

} // namespace velora
