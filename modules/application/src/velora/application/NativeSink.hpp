#pragma once

#include <string>
#include <velora/composition/FrameScheduler.hpp>
#include <velora/composition/CompositionRenderer.hpp>
#include <velora/graphics/Graphics.hpp>

namespace velora
{

// FrameSink implementation that presents composition snapshots through a GraphicsSurface.
// Owns the surface and renderer; the graphics device must outlive the sink.
class NativeSink final : public FrameSink
{
public:
    NativeSink() = default;
    ~NativeSink() override;
    NativeSink(const NativeSink&) = delete;
    NativeSink& operator=(const NativeSink&) = delete;

    [[nodiscard]] bool attach(GraphicsDevice& device, void* nativeWindow, const SurfaceConfig& config);
    void detach() noexcept;
    [[nodiscard]] bool isAttached() const noexcept;

    // Resize the surface to match window dimensions.
    [[nodiscard]] bool resize(const SurfaceConfig& config);

    // FrameSink interface: present a snapshot to the attached surface.
    bool present(const SceneSnapshot& scene, const std::string* capturePath) override;

    const std::string& lastError() const noexcept;

private:
    GraphicsSurface m_surface;
    CompositionRenderer m_renderer;
    std::string m_error;
};

} // namespace velora
