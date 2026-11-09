// Core, platform-stable scalar types shared by Velora modules.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

namespace velora
{
using i8 = std::int8_t;
using u8 = std::uint8_t;
using i16 = std::int16_t;
using u16 = std::uint16_t;
using i32 = std::int32_t;
using u32 = std::uint32_t;
using i64 = std::int64_t;
using u64 = std::uint64_t;
using f32 = float;
using f64 = double;

using usize = std::size_t;
using isize = std::ptrdiff_t;
using uptr = std::uintptr_t;
using iptr = std::intptr_t;
using byte = std::byte;
using StringView = std::string_view;

inline constexpr u32 kInvalidIndex = std::numeric_limits<u32>::max();
inline constexpr usize kPointerSize = sizeof(void*);

static_assert(sizeof(i8) == 1 && sizeof(u8) == 1);
static_assert(sizeof(i16) == 2 && sizeof(u16) == 2);
static_assert(sizeof(i32) == 4 && sizeof(u32) == 4);
static_assert(sizeof(i64) == 8 && sizeof(u64) == 8);
static_assert(sizeof(f32) == 4 && sizeof(f64) == 8);
static_assert(sizeof(uptr) == sizeof(void*));
}
