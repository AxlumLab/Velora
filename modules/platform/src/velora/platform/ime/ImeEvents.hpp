#pragma once

#include <string>
#include <velora/globals.hpp>

// Platform HAL — IME and text composition contract.
//
// The platform reports composition state as UTF-8 with *byte* offsets into that UTF-8 text,
// not UTF-16 units. Callers that hand these offsets to a text model get byte offsets, which is
// what the runtime's TextBox editing state uses.

namespace velora
{

enum class TextInputKind
{
    kPreedit,
    kCommit
};

struct TextInputEvent
{
    TextInputKind kind;
    std::string text;
    int cursor = 0;
    int selectionLength = 0;
};

// Which IME implementation is active. kNone means the window has no text input at all.
enum class TextInputBackend
{
    kNone,
    kTsf,
    kImm
};

} // namespace velora
