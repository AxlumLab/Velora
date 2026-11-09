#include "velora/application/NativeSink.hpp"

#include <velora/composition/Compositor.hpp>

namespace velora
{

NativeSink::~NativeSink()
{
    detach();
}

bool NativeSink::attach(GraphicsDevice& device, void* nativeWindow, const SurfaceConfig& config)
{
    if (m_surface.isAttached())
    {
        m_error = "NativeSink is already attached";
        return false;
    }
    if (!m_surface.attach(device, nativeWindow, config))
    {
        m_error = m_surface.lastError();
        return false;
    }
    m_error.clear();
    return true;
}

void NativeSink::detach() noexcept
{
    m_surface.detach();
}

bool NativeSink::isAttached() const noexcept
{
    return m_surface.isAttached();
}

bool NativeSink::resize(const SurfaceConfig& config)
{
    if (!m_surface.isAttached())
    {
        m_error = "NativeSink is not attached";
        return false;
    }
    if (!m_surface.resize(config))
    {
        m_error = m_surface.lastError();
        return false;
    }
    m_error.clear();
    return true;
}

bool NativeSink::present(const SceneSnapshot& scene, const std::string* capturePath)
{
    if (!m_surface.isAttached())
    {
        m_error = "NativeSink is not attached";
        return false;
    }
    if (!m_renderer.present(m_surface, scene))
    {
        m_error = m_surface.lastError();
        return false;
    }
    m_error.clear();
    return true;
}

const std::string& NativeSink::lastError() const noexcept
{
    return m_error;
}

} // namespace velora
