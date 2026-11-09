#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <windows.h>

namespace velora
{

std::wstring toUtf16(std::string_view text);
std::string toUtf8(std::wstring_view text);

// TSF composition client: an ITfContextOwner over TSF's default text store. Committed
// text is reported and removed from the store, so the store only ever holds the
// active composition.
class TsfTextInput
{
public:
    struct Callbacks
    {
        std::function<void(const std::wstring& text, int cursor, int selection)> preedit;
        std::function<void(const std::wstring& text)> commit;
        std::function<RECT()> caretScreenRect;
    };

    // Returns null when TSF is unavailable on this thread (no STA, no thread manager).
    static std::unique_ptr<TsfTextInput> create(HWND hwnd, Callbacks callbacks);
    virtual ~TsfTextInput() = default;
    virtual void setEnabled(bool enabled) = 0;
    virtual void destroy() = 0;
};

} // namespace velora
