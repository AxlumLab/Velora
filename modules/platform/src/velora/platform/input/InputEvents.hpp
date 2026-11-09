#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <velora/globals.hpp>

// Platform HAL — input event contract.
//
// The platform layer reports already-translated input events. It never routes events to a
// control, never runs hit testing and never resolves focus: those belong to the UI runtime.
// Coordinates are physical client pixels; divide by NativeWindow::dpiScale() for DIPs.

namespace velora
{

// Modifier keys held when an input event happened.
enum KeyModifier : u32
{
    kModifierNone = 0,
    kModifierShift = 1,
    kModifierControl = 2,
    kModifierAlt = 4,
};

enum class PointerButton : u32
{
    kNone = 0,
    kLeft = 1,
    kRight = 2,
    kMiddle = 3
};

enum class WindowEventKind
{
    kResized,
    kDpiChanged,
    kMinimized,
    kRestored,
    kShown,
    kHidden,
    kFocusGained,
    kFocusLost,
    kCloseRequested, // set WindowEvent::cancel in a sink to keep the window open
    kClosed,
    kKeyDown,
    kKeyUp,
    kMouseMove,
    kMouseButtonDown,
    kMouseButtonUp,
    kMouseWheel,
    kFileDrop
};

// One platform input or lifecycle event. Positions and sizes are physical client pixels.
struct WindowEvent
{
    WindowEventKind kind;
    int width = 0;
    int height = 0;
    u32 code = 0; // virtual key, or mouse button (see PointerButton)
    int x = 0;
    int y = 0;
    float dpiScale = 0;
    float wheel = 0;
    std::vector<std::string> paths;
    u32 modifiers = kModifierNone;
    bool cancel = false;
};

} // namespace velora
