#pragma once

#include <velora/globals.hpp>

// Platform HAL — the surface a renderer draws into.
//
// SurfaceDescriptor is the *platform* half of a render target: the borrowed native handle plus
// the size/DPI facts the backend needs. It deliberately carries no graphics device, no
// swap chain and no compositor: those are `velora::graphics` and `velora::composition` types,
// and the platform module must not depend on them.
//
// A renderer that needs to create a swap chain takes a SurfaceDescriptor, not a raw void*,
// so a Win32 HWND is never confused with some other backend's handle.

namespace velora
{

struct SurfaceDescriptor
{
    // Borrowed, owned by the window. Null means "no surface yet"; backends must reject it
    // instead of creating an unattached target.
    void* handle = nullptr;
    // Swap chain size in physical device pixels (not DIPs).
    u32 width = 0;
    u32 height = 0;
    // Device pixels per DIP, used to scale DIP-space display lists onto the target.
    f32 dpiScale = 1.0f;
    bool transparent = false;
};

} // namespace velora
