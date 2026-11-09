#pragma once

#include <algorithm>
#include <cstdint>

namespace velora
{
struct Color
{
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;

    constexpr Color() = default;
    constexpr Color(float red, float green, float blue, float alpha = 1) : r(red), g(green), b(blue), a(alpha)
    {
    }

    constexpr Color premultiplied() const
    {
        return {r * a, g * a, b * a, a};
    }
};

struct Rect
{
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

struct Transform
{
    float translateX = 0;
    float translateY = 0;
    float scaleX = 1;
    float scaleY = 1;
    float rotationDegrees = 0;
};

struct Point
{
    float x = 0;
    float y = 0;
};

enum class PathVerb
{
    kMoveTo,
    kLineTo,
    kQuadTo,
    kCubicTo,
    kClose
};

struct PathElement
{
    PathVerb verb = PathVerb::kMoveTo;
    Point p1{}, p2{}, p3{};
};

struct Paint
{
    Color color{};
    float strokeWidth = 0;
    bool stroke = false;
};

enum class UnsupportedFeature
{
    kImage,
    kGlyphRun,
    kShadow,
    kBackdropFilter
};
}
