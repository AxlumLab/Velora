// CRC-32 (the zlib/PNG polynomial), used by Velora's binary formats to detect corruption:
// the .vxbf compiled-XAML container and the .vpri/.vpak resource containers all carry a
// checksum over everything after the header, so a truncated or edited file is refused rather
// than read as garbage.
//
// It is a corruption check, not a signature: the formats are not a trust boundary.
#pragma once

#include <span>
#include <velora/globals.hpp>

namespace velora
{

// The CRC-32 of `bytes` (init 0xFFFFFFFF, reflected, final xor).
[[nodiscard]] u32 crc32(std::span<const u8> bytes) noexcept;

} // namespace velora
