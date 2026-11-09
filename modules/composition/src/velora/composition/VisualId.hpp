#pragma once

#include <velora/globals.hpp>

// Identity of one visual in a Compositor's tree.
//
// A separate declaration lets modules name visuals without including the Compositor
// implementation. VisualBridge associates elements with these ids and forward-declares
// Compositor in its public header. 0 never names a visual.

namespace velora
{

using VisualId = u64;

inline constexpr VisualId kNoVisual = 0;

} // namespace velora
