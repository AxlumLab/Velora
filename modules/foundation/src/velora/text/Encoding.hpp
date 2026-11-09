// UTF-8/UTF-16 conversion utilities with platform-independent code units.
// Adapted from Toolkit CoreKit Encoding.
// Copyright (c) 2026 FlybirdGames. All rights reserved.
#pragma once

#include <string>
#include <string_view>
#include <velora/globals.hpp>

namespace velora
{
// Strict conversion preserves embedded NULs. Throws std::runtime_error on invalid UTF-8.
std::u16string utf8ToUtf16(std::string_view utf8);

// Throws std::runtime_error on unpaired UTF-16 surrogates.
std::string utf16ToUtf8(std::u16string_view utf16);

// These convenience wrappers return an empty string on conversion or allocation failure.
std::u16string utf8ToUtf16Safe(std::string_view utf8) noexcept;
std::string utf16ToUtf8Safe(std::u16string_view utf16) noexcept;
}
