// Newly authored regression input. See README.md for provenance.
#pragma once

#include <velora/globals.hpp>
#include <velora/reflect/Markers.hpp>

namespace rkgen_test
{

using velora::u8;

rkenum class Color : u8
{
    Red,
    Green = 2,
    Blue
};

rkstruct Vec3
{
    float x = 0;
    float y = 0;
    float z = 0;
};

} // namespace rkgen_test
