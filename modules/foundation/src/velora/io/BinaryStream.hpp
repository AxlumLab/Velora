// Adapted from Toolkit CoreKit MemoryStream for Velora resource serialization.
// Copyright (c) 2026 FlybirdGames. All rights reserved.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <velora/globals.hpp>

namespace velora
{
class BinaryWriter final
{
public:
    void u8v(u8 value);
    void u16v(u16 value);
    void u32v(u32 value);
    void u64v(u64 value);
    void text(std::string_view value);
    void raw(std::span<const u8> value);

    std::vector<u8>& bytes() noexcept;
    const std::vector<u8>& bytes() const noexcept;
    std::vector<u8> takeBytes() noexcept;

private:
    void writeLittleEndian(u64 value, u8 size);

    std::vector<u8> m_bytes;
};

class BinaryReader final
{
public:
    explicit BinaryReader(std::span<const u8> bytes) noexcept;

    bool ok() const noexcept;
    bool atEnd() const noexcept;
    usize left() const noexcept;
    usize position() const noexcept;
    bool seek(usize position) noexcept;

    u8 u8v() noexcept;
    u16 u16v() noexcept;
    u32 u32v() noexcept;
    u64 u64v() noexcept;
    std::string text();
    std::span<const u8> raw(usize size) noexcept;

private:
    bool canRead(usize size) noexcept;
    u64 readLittleEndian(u8 size) noexcept;

    std::span<const u8> m_bytes;
    usize m_position = 0;
    bool m_ok = true;
};
}
