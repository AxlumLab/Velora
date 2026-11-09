#pragma once

#include "Types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <velora/globals.hpp>

namespace velora
{
struct ClearCommand
{
    Color color;
};
struct RectCommand
{
    Rect bounds;
    Paint paint;
};
struct RoundedRectCommand
{
    Rect bounds;
    float radius = 0;
    Paint paint;
};
struct PathCommand
{
    std::vector<PathElement> elements;
    Paint paint;
};
struct ClipRectCommand
{
    Rect bounds;
};
struct ClipRoundedRectCommand
{
    Rect bounds;
    float radius = 0;
};
struct TransformCommand
{
    Transform transform;
};
struct RestoreCommand
{
};
struct SaveCommand
{
};
struct OpacityCommand
{
    float value = 1;
};

// Overlapping children compose as one group. Alpha is premultiplied.
struct SaveLayerCommand
{
    Rect bounds;
    float opacity = 1;
    float blurSigma = 0;
    std::optional<Color> tint;
};

struct CommandList;
struct LayerRef
{
    u64 id = 0;
    u64 version = 0;
    Rect bounds;
    std::shared_ptr<const CommandList> commands;
};

struct TextCommand
{
    Rect bounds;
    std::string value;
    float fontSize = 24;
    Paint paint;
};
enum class ImageFit
{
    kContain,
    kCover,
    kStretch
};
struct ImageCommand
{
    Rect bounds;
    std::string path;
    ImageFit fit = ImageFit::kContain;
};
struct UnsupportedCommand
{
    UnsupportedFeature feature;
};

using Command =
    std::variant<ClearCommand, RectCommand, RoundedRectCommand, PathCommand, ClipRectCommand,
                 ClipRoundedRectCommand, TransformCommand, SaveCommand, SaveLayerCommand, LayerRef,
                 RestoreCommand, OpacityCommand, TextCommand, ImageCommand, UnsupportedCommand>;

struct CommandList
{
    std::vector<Command> commands;
};

class DisplayList final
{
public:
    explicit DisplayList(std::vector<Command> commands) : m_commands(std::move(commands)) {}

    const std::vector<Command>& commands() const noexcept
    {
        return m_commands;
    }

private:
    std::vector<Command> m_commands;
};
}
