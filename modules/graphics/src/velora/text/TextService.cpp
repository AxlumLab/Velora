#include "velora/text/TextService.hpp"

#include <vector>

#if defined(_WIN32)
#include "velora/text/windows/DWriteText.hpp"
#endif

namespace velora
{

struct TextService::Impl
{
    bool initialized = false;
    std::string error;
};

TextService::TextService() : m_impl(std::make_unique<Impl>()) {}

TextService::~TextService() = default;

bool TextService::initialize() noexcept
{
#if defined(_WIN32)
    if (!dwriteFactory())
    {
        m_impl->error = "DWriteCreateFactory failed";
        return false;
    }
    m_impl->error.clear();
    m_impl->initialized = true;
    return true;
#else
    m_impl->error = "no text backend on this platform";
    return false;
#endif
}

bool TextService::isReady() const noexcept
{
    return m_impl->initialized;
}

const std::string& TextService::lastError() const noexcept
{
    return m_impl->error;
}

std::string TextService::resolveFamily(std::string_view requested) const
{
#if defined(_WIN32)
    const std::wstring family = resolveFontFamily(requested);
    if (family.empty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, family.data(),
                                          static_cast<int>(family.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0)
        return {};
    std::string result(static_cast<usize>(count), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, family.data(),
                             static_cast<int>(family.size()), result.data(), count, nullptr, nullptr))
        return {};
    return result;
#else
    return std::string(requested);
#endif
}

bool TextService::measureText(std::string_view utf8, const FontDescriptor& font,
                              TextMetrics& result, f32 maxWidthDips, bool wordWrap) const noexcept
{
#if defined(_WIN32)
    if (!m_impl->initialized)
        return false;
    const auto layout = createTextLayout(utf8, font, maxWidthDips, wordWrap);
    DWRITE_TEXT_METRICS metrics{};
    if (!layout || FAILED(layout->GetMetrics(&metrics)))
        return false;
    UINT32 lines = 0;
    // First query the required array size, then retrieve all lines so multi-line text is safe.
    layout->GetLineMetrics(nullptr, 0, &lines);
    std::vector<DWRITE_LINE_METRICS> lineMetrics(lines);
    if (lines && FAILED(layout->GetLineMetrics(lineMetrics.data(), lines, &lines)))
        return false;
    result.widthDips = metrics.widthIncludingTrailingWhitespace;
    result.heightDips = metrics.height;
    result.baselineDips = lineMetrics.empty() ? 0.0f : lineMetrics.front().baseline;
    result.lineCount = metrics.lineCount;
    return true;
#else
    (void)utf8;
    (void)font;
    (void)result;
    (void)maxWidthDips;
    (void)wordWrap;
    return false;
#endif
}

f32 TextService::measureWidth(std::string_view utf8, f32 sizeDips) const noexcept
{
    FontDescriptor font;
    font.sizeDips = sizeDips;
    TextMetrics metrics;
    return measureText(utf8, font, metrics) ? metrics.widthDips : 0.0f;
}

void TextService::shutdown() noexcept
{
    m_impl->initialized = false;
}

} // namespace velora
