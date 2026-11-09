#include <limits>
#include <stdexcept>
#include <utility>
#include <velora/io/BinaryStream.hpp>

namespace velora
{
void BinaryWriter::u8v(u8 value)
{
    m_bytes.push_back(value);
}

void BinaryWriter::u16v(u16 value)
{
    writeLittleEndian(value, 2);
}

void BinaryWriter::u32v(u32 value)
{
    writeLittleEndian(value, 4);
}

void BinaryWriter::u64v(u64 value)
{
    writeLittleEndian(value, 8);
}

void BinaryWriter::writeLittleEndian(u64 value, u8 size)
{
    for (u8 i = 0; i < size; ++i)
    {
        m_bytes.push_back(static_cast<u8>(value >> (8 * i)));
    }
}

void BinaryWriter::text(std::string_view value)
{
    if (value.size() > std::numeric_limits<u32>::max())
    {
        throw std::length_error("BinaryWriter string exceeds u32 length");
    }
    u32v(static_cast<u32>(value.size()));
    m_bytes.insert(m_bytes.end(), value.begin(), value.end());
}

void BinaryWriter::raw(std::span<const u8> value)
{
    m_bytes.insert(m_bytes.end(), value.begin(), value.end());
}

std::vector<u8>& BinaryWriter::bytes() noexcept
{
    return m_bytes;
}

const std::vector<u8>& BinaryWriter::bytes() const noexcept
{
    return m_bytes;
}

std::vector<u8> BinaryWriter::takeBytes() noexcept
{
    return std::move(m_bytes);
}

BinaryReader::BinaryReader(std::span<const u8> bytes) noexcept : m_bytes(bytes)
{
}

bool BinaryReader::ok() const noexcept
{
    return m_ok;
}

bool BinaryReader::atEnd() const noexcept
{
    return m_position == m_bytes.size();
}

usize BinaryReader::left() const noexcept
{
    return m_bytes.size() - m_position;
}

usize BinaryReader::position() const noexcept
{
    return m_position;
}

bool BinaryReader::seek(usize position) noexcept
{
    if (position > m_bytes.size())
    {
        m_ok = false;
        return false;
    }
    m_position = position;
    return m_ok;
}

bool BinaryReader::canRead(usize size) noexcept
{
    if (!m_ok || size > left())
    {
        m_ok = false;
    }
    return m_ok;
}

u64 BinaryReader::readLittleEndian(u8 size) noexcept
{
    if (!canRead(size))
    {
        return 0;
    }
    u64 result = 0;
    for (u8 i = 0; i < size; ++i)
    {
        result |= static_cast<u64>(m_bytes[m_position++]) << (8 * i);
    }
    return result;
}

u8 BinaryReader::u8v() noexcept
{
    return static_cast<u8>(readLittleEndian(1));
}

u16 BinaryReader::u16v() noexcept
{
    return static_cast<u16>(readLittleEndian(2));
}

u32 BinaryReader::u32v() noexcept
{
    return static_cast<u32>(readLittleEndian(4));
}

u64 BinaryReader::u64v() noexcept
{
    return readLittleEndian(8);
}

std::string BinaryReader::text()
{
    const u32 size = u32v();
    if (!canRead(size))
    {
        return {};
    }
    const auto* start = reinterpret_cast<const char*>(m_bytes.data() + m_position);
    m_position += size;
    return std::string(start, size);
}

std::span<const u8> BinaryReader::raw(usize size) noexcept
{
    if (!canRead(size))
    {
        return {};
    }
    const auto chunk = m_bytes.subspan(m_position, size);
    m_position += size;
    return chunk;
}
}
