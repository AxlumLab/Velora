#pragma once

#include <velora/globals.hpp>

namespace velora
{

enum class Key
{
    kLeft,
    kRight,
    kHome,
    kEnd,
    kBackspace,
    kDelete,
    kEnter,
    kA,
    kUp,
    kDown,
    kPageUp,
    kPageDown,
    kTab
};

struct RoutedEventArgs
{
    virtual ~RoutedEventArgs() = default;
    u64 originalSource = 0;
    bool handled = false;
};

// Coordinates in DIPs. x/y are relative to the current sender; windowX/windowY stay fixed
// throughout the route. The current input backend reports one primary pointer.
struct PointerEventArgs : RoutedEventArgs
{
    f32 x = 0, y = 0;
    f32 windowX = 0, windowY = 0;
};

struct KeyEventArgs : RoutedEventArgs
{
    Key key = Key::kEnter;
    bool shift = false;
    bool ctrl = false;
};

} // namespace velora
