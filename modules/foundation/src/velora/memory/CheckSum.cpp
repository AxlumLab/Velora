#include <array>
#include <velora/memory/CheckSum.hpp>

namespace velora
{

namespace
{
const std::array<u32, 256>& crcTable()
{
    static const std::array<u32, 256> table = []
    {
        std::array<u32, 256> t{};
        for (u32 i = 0; i < 256; ++i)
        {
            u32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}
} // namespace

u32 crc32(std::span<const u8> bytes) noexcept
{
    const auto& table = crcTable();
    u32 c = 0xFFFFFFFFu;
    for (const u8 b : bytes)
        c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

} // namespace velora
