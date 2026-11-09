#pragma once

#include <optional>
#include <span>
#include <vector>
#include <velora/globals.hpp>

// Raw DEFLATE (RFC 1951, no zlib or gzip wrapper) for .vpak payloads (R08.3).
//
// `deflateRaw` is a greedy LZ77 matcher over a 32 KiB window, emitted as fixed-Huffman blocks. It
// is not tuned for ratio; it exists so packages can hold compressed text and markup without a
// third-party dependency. `inflateRaw` decodes every block type (stored, fixed and dynamic
// Huffman), so it reads streams from other encoders too (zlib with wbits=-15, for example).
//
// Both are bounded: inflate refuses to produce more than `expectedSize` bytes and refuses a stream
// that ends before reaching it, so a damaged or hostile payload cannot expand without limit.

namespace velora
{

[[nodiscard]] std::vector<u8> deflateRaw(std::span<const u8> bytes);

// The decompressed bytes, or nullopt when the stream is malformed, ends early, or does not decode
// to exactly `expectedSize` bytes.
[[nodiscard]] std::optional<std::vector<u8>> inflateRaw(std::span<const u8> bytes, u64 expectedSize);

} // namespace velora
