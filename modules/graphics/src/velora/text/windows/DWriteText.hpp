#pragma once

// Internal to the HAL (text and graphics modules on Windows): DirectWrite layouts shared by
// measurement and drawing. Not part of the application API.
#include <dwrite_2.h>
#include <string_view>
#include <velora/text/TextService.hpp>
#include <wrl/client.h>

namespace velora
{

// The process-wide shared DirectWrite factory; null when DirectWrite is unavailable.
IDWriteFactory2* dwriteFactory();
// An installed family for `requested`: itself, else Segoe UI, Microsoft YaHei UI, Arial.
std::wstring resolveFontFamily(std::string_view requested);
// DirectWrite line metrics, locale, wrapping and optional width constraint.
Microsoft::WRL::ComPtr<IDWriteTextLayout> createTextLayout(std::string_view utf8, const FontDescriptor& font,
                                                           float maxWidth, bool wordWrap = false);
std::wstring utf8ToWide(std::string_view utf8);

} // namespace velora
