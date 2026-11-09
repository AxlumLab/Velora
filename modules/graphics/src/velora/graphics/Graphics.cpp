// The graphics HAL on platforms without a backend: every operation reports that it is unavailable.
#include <velora/graphics/Graphics.hpp>

namespace velora
{

namespace
{
const std::string kUnavailable = "no graphics backend on this platform";
}

struct GraphicsDevice::Impl
{
};

struct GraphicsSurface::Impl
{
    SurfaceConfig config;
    DrawStats stats;
};

bool saveCapturePng(const FrameCapture&, const std::string&)
{
    return false;
}

GraphicsDevice::GraphicsDevice() : m_impl(std::make_unique<Impl>()) {}
GraphicsDevice::~GraphicsDevice() = default;
bool GraphicsDevice::initialize() noexcept
{
    return false;
}
void GraphicsDevice::shutdown() noexcept {}
bool GraphicsDevice::isReady() const noexcept
{
    return false;
}
GraphicsBackend GraphicsDevice::getBackend() const noexcept
{
    return GraphicsBackend::kUnavailable;
}
const std::string& GraphicsDevice::lastError() const noexcept
{
    return kUnavailable;
}
const std::string& GraphicsDevice::getAdapterName() const noexcept
{
    static const std::string none;
    return none;
}
bool GraphicsDevice::isSoftware() const noexcept
{
    return false;
}
u64 GraphicsDevice::getGeneration() const noexcept
{
    return 0;
}
bool GraphicsDevice::simulateDeviceLoss() noexcept
{
    return false;
}

GraphicsSurface::GraphicsSurface() : m_impl(std::make_unique<Impl>()) {}
GraphicsSurface::~GraphicsSurface() = default;
bool GraphicsSurface::attach(GraphicsDevice&, void*, const SurfaceConfig&) noexcept
{
    return false;
}
bool GraphicsSurface::resize(const SurfaceConfig&) noexcept
{
    return false;
}
bool GraphicsSurface::draw(const DisplayList&, FrameCapture*) noexcept
{
    return false;
}
bool GraphicsSurface::isAttached() const noexcept
{
    return false;
}
void GraphicsSurface::detach() noexcept {}
const SurfaceConfig& GraphicsSurface::getConfig() const noexcept
{
    return m_impl->config;
}
const DrawStats& GraphicsSurface::getLastStats() const noexcept
{
    return m_impl->stats;
}
const std::string& GraphicsSurface::lastError() const noexcept
{
    return kUnavailable;
}
u64 GraphicsSurface::getPresentCount() const noexcept
{
    return 0;
}
u64 GraphicsSurface::getDeviceRecoveries() const noexcept
{
    return 0;
}

} // namespace velora
