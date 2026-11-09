#pragma once

#include <velora/graphics/Graphics.hpp>

namespace velora
{
struct SceneSnapshot;
}

namespace velora
{
class CompositionRenderer final
{
public:
    [[nodiscard]] bool present(GraphicsSurface& surface, const SceneSnapshot& snapshot) noexcept;
};
}
