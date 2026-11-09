#pragma once

#include <span>
#include <string>
#include <variant>
#include <vector>
#include <velora/xaml/XamlIR.hpp>

// The .vxbf binary XAML format (R06.3). Velora's own, versioned format; no compatibility with
// Microsoft's XBF is claimed.
//
// Layout (all integers little-endian):
//   header     magic "VXBF" | u16 major | u16 minor | u32 flags | u64 schema fingerprint |
//              u32 section count | u32 CRC-32 of every byte after the header
//   directory  section count x { u32 kind | u32 offset | u32 size }   (offsets from file start)
//   sections   Meta, Strings, Types, Properties, Values, Instructions
//
// The reader checks the magic, version, size quotas, every offset and count against the file
// bounds, the checksum and finally the IR structure (validate), so a truncated, corrupted or
// foreign file is rejected with a message and never partially loaded.

namespace velora
{

inline constexpr u16 kVxbfMajor = 1;
inline constexpr u16 kVxbfMinor = 3; // 1.3 typed DataTemplate accessors (older files still read)
inline constexpr usize kVxbfMaxBytes = 64u * 1024u * 1024u;
inline constexpr usize kVxbfMaxRecords = 1u << 20; // per table
inline constexpr usize kVxbfMaxDepth = 256;

[[nodiscard]] std::vector<u8> writeVxbf(const XamlIR& ir);

struct VxbfError
{
    std::string message;
};

// The IR, or why the bytes are not an acceptable .vxbf. `expectedFingerprint` 0 accepts any schema
// (tools that only dump); loaders pass their schema's fingerprint.
[[nodiscard]] std::variant<XamlIR, VxbfError> readVxbf(std::span<const u8> bytes,
                                                       u64 expectedFingerprint = 0);

} // namespace velora
