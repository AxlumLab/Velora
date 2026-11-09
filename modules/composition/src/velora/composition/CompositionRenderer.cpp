#include <velora/composition/CompositionRenderer.hpp>
#include <velora/composition/Compositor.hpp>

namespace velora
{
bool CompositionRenderer::present(GraphicsSurface& surface, const SceneSnapshot& snapshot) noexcept
{
    if (!snapshot.displayList)
    {
        return false;
    }
    return surface.draw(*snapshot.displayList);
}
}
