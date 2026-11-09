#pragma once

#include <string_view>
#include <velora/globals.hpp>

// Name-derived identities used by runtime type metadata and compiled XAML. They identify
// declarations, not object instances, interfaces or a binary compatibility contract.
namespace velora
{

struct Uuid
{
    u64 high = 0;
    u64 low = 0;

    constexpr bool operator==(const Uuid&) const noexcept = default;
};

struct TypeId
{
    Uuid value{};

    constexpr bool operator==(const TypeId&) const noexcept = default;
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return value.high == 0 && value.low == 0;
    }
};

inline constexpr Uuid kVeloraUuidNamespace{0xEE8D0F91B2DE4CB2ULL, 0x9455AD7B83F70A26ULL};

// RFC 4122 name-based UUID, version 5. The halves contain bytes 0-7 and 8-15 in big-endian order.
[[nodiscard]] Uuid uuidV5(const Uuid& nameSpace, std::string_view name) noexcept;

// Uses the existing "Velora.Class." prefix so compiled XAML keeps the same type identities.
[[nodiscard]] TypeId typeIdFor(std::string_view className) noexcept;

} // namespace velora
