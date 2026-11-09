#include <array>
#include <initializer_list>
#include <velora/types/TypeId.hpp>

namespace velora
{

namespace
{
// SHA-1 (FIPS 180-4). Used only to derive name-based UUIDs; not a security primitive here.
class Sha1
{
public:
    void update(const u8* data, usize size) noexcept
    {
        for (usize i = 0; i < size; ++i)
        {
            m_block[m_used++] = data[i];
            if (m_used == m_block.size())
            {
                compress();
                m_used = 0;
            }
        }
        m_bits += static_cast<u64>(size) * 8;
    }

    std::array<u8, 20> finish() noexcept
    {
        const u64 bits = m_bits;
        const u8 pad = 0x80;
        update(&pad, 1);
        const u8 zero = 0;
        while (m_used != 56)
            update(&zero, 1);
        u8 length[8];
        for (int i = 0; i < 8; ++i)
            length[i] = static_cast<u8>(bits >> (56 - 8 * i));
        update(length, 8);

        std::array<u8, 20> digest{};
        for (int i = 0; i < 5; ++i)
            for (int j = 0; j < 4; ++j)
                digest[static_cast<usize>(4 * i + j)] =
                    static_cast<u8>(m_state[static_cast<usize>(i)] >> (24 - 8 * j));
        return digest;
    }

private:
    static u32 rotate(u32 value, int count) noexcept
    {
        return (value << count) | (value >> (32 - count));
    }

    void compress() noexcept
    {
        u32 w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (u32{m_block[static_cast<usize>(4 * i)]} << 24) |
                   (u32{m_block[static_cast<usize>(4 * i + 1)]} << 16) |
                   (u32{m_block[static_cast<usize>(4 * i + 2)]} << 8) |
                   u32{m_block[static_cast<usize>(4 * i + 3)]};
        for (int i = 16; i < 80; ++i)
            w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        u32 a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3], e = m_state[4];
        for (int i = 0; i < 80; ++i)
        {
            u32 f, k;
            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const u32 next = rotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotate(b, 30);
            b = a;
            a = next;
        }
        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
    }

    std::array<u32, 5> m_state{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::array<u8, 64> m_block{};
    usize m_used = 0;
    u64 m_bits = 0;
};

void appendBigEndian(u64 value, u8* out) noexcept
{
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<u8>(value >> (56 - 8 * i));
}

u64 readBigEndian(const u8* in) noexcept
{
    u64 value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | in[i];
    return value;
}

// Name-based UUID v5 over the concatenation of `parts`, without building the joined string.
Uuid uuidV5Parts(const Uuid& nameSpace, std::initializer_list<std::string_view> parts) noexcept
{
    u8 prefix[16];
    appendBigEndian(nameSpace.high, prefix);
    appendBigEndian(nameSpace.low, prefix + 8);

    Sha1 sha;
    sha.update(prefix, sizeof(prefix));
    for (const auto part : parts)
        sha.update(reinterpret_cast<const u8*>(part.data()), part.size());
    auto digest = sha.finish();

    digest[6] = static_cast<u8>((digest[6] & 0x0F) | 0x50); // version 5
    digest[8] = static_cast<u8>((digest[8] & 0x3F) | 0x80); // RFC 4122 variant
    return {readBigEndian(digest.data()), readBigEndian(digest.data() + 8)};
}
} // namespace

Uuid uuidV5(const Uuid& nameSpace, std::string_view name) noexcept
{
    return uuidV5Parts(nameSpace, {name});
}

TypeId typeIdFor(std::string_view className) noexcept
{
    // Keep the class-name prefix stable for compiled XAML and resource metadata.
    return {uuidV5Parts(kVeloraUuidNamespace, {"Velora.Class.", className})};
}

} // namespace velora
