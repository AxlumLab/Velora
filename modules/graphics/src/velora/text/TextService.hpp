#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <velora/globals.hpp>

namespace velora
{

struct FontDescriptor
{
    std::string family = "Segoe UI"; // falls back to an installed UI font when missing
    f32 sizeDips = 14.0f;
    u16 weight = 400;
    bool italic = false;
    std::string locale = "en-US";
};

// One line of text as layout and drawing see it: the line box is lineHeight(size) DIPs tall
// (the same 1.33 x size the runtime layout uses) with the baseline at baselineDips.
struct TextMetrics
{
    f32 widthDips = 0.0f;
    f32 heightDips = 0.0f;
    f32 baselineDips = 0.0f;
    u32 lineCount = 0;
};

// Text services on DirectWrite (Windows): font resolution with system fallback for scripts
// and emoji the primary family lacks, shaping, and single-line metrics. Graphics draws text
// with the same layouts, so measured and drawn text agree.
class TextService final
{
public:
    TextService();
    ~TextService();

    TextService(const TextService&) = delete;
    TextService& operator=(const TextService&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;
    // The family actually used for `requested` (the request itself when installed).
    [[nodiscard]] std::string resolveFamily(std::string_view requested) const;
    [[nodiscard]] bool measureText(std::string_view utf8, const FontDescriptor& font,
                                   TextMetrics& result, f32 maxWidthDips = 0.0f,
                                   bool wordWrap = false) const noexcept;
    // Width of one line of UTF-8 text in DIPs (0 on failure); fits the runtime's TextMeasurer.
    [[nodiscard]] f32 measureWidth(std::string_view utf8, f32 sizeDips) const noexcept;
    void shutdown() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace velora
