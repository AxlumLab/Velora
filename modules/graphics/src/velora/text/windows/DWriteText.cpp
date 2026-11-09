// DirectWrite layouts: one shared factory, font resolution, the runtime's line box.
#include "velora/text/windows/DWriteText.hpp"

#include <windows.h>

namespace velora
{

using Microsoft::WRL::ComPtr;

namespace
{

bool installed(IDWriteFontCollection* fonts, const std::wstring& family)
{
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(fonts->FindFamilyName(family.c_str(), &index, &exists)) && exists;
}

} // namespace

std::wstring utf8ToWide(std::string_view utf8)
{
    if (utf8.empty())
        return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (count <= 0)
        return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                             static_cast<int>(utf8.size()), out.data(), count))
        return {};
    return out;
}

IDWriteFactory2* dwriteFactory()
{
    static ComPtr<IDWriteFactory2> factory = []
    {
        ComPtr<IDWriteFactory2> f;
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2),
                            reinterpret_cast<IUnknown**>(f.GetAddressOf()));
        return f;
    }();
    return factory.Get();
}

std::wstring resolveFontFamily(std::string_view requested)
{
    IDWriteFactory2* factory = dwriteFactory();
    ComPtr<IDWriteFontCollection> fonts;
    if (!factory || FAILED(factory->GetSystemFontCollection(&fonts)))
        return utf8ToWide(requested);
    const std::wstring wanted = utf8ToWide(requested);
    if (!wanted.empty() && installed(fonts.Get(), wanted))
        return wanted;
    for (const wchar_t* candidate : {L"Segoe UI", L"Microsoft YaHei UI", L"Arial"})
        if (installed(fonts.Get(), candidate))
            return candidate;
    return wanted;
}

ComPtr<IDWriteTextLayout> createTextLayout(std::string_view utf8, const FontDescriptor& font,
                                          float maxWidth, bool wordWrap)
{
    IDWriteFactory2* factory = dwriteFactory();
    if (!factory)
        return nullptr;
    const std::wstring family = resolveFontFamily(font.family);
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(factory->CreateTextFormat(
            family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(font.weight),
            font.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, font.sizeDips, utf8ToWide(font.locale).c_str(), &format)))
        return nullptr;
    format->SetWordWrapping(wordWrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    const std::wstring text = utf8ToWide(utf8);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
                                         maxWidth > 0 ? maxWidth : 1e6f, 1e6f, &layout)))
        return nullptr;
    return layout;
}

} // namespace velora
