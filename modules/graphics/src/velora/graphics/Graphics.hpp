#pragma once

#include <memory>
#include <string>
#include <vector>
#include <velora/globals.hpp>

namespace velora
{

class DisplayList;

enum class GraphicsBackend : u8
{
    kUnavailable,
    kDirect2D
};

struct PixelSize
{
    u32 width = 0;
    u32 height = 0;
};

struct SurfaceConfig
{
    PixelSize size;      // swap chain size in device pixels
    f32 dpiScale = 1.0f; // device pixels per DIP; display lists are drawn in DIPs
    bool transparent = false;
};

// A window's device-pixel size at a given DIP scale, with no backend handle. This is the
// graphics half of a render target; the platform half is velora::SurfaceDescriptor, which
// velora::application converts into this (graphics must not depend on the platform module).
[[nodiscard]] inline SurfaceConfig surfaceConfig(u32 widthPixels, u32 heightPixels, f32 dpiScale,
                                                 bool transparent = false)
{
    return SurfaceConfig{{widthPixels, heightPixels}, dpiScale, transparent};
}

// What one draw() did. A command that could not be drawn is counted, never skipped silently.
struct DrawStats
{
    u32 commands = 0;
    u32 unsupported = 0;     // UnsupportedCommand and commands the backend failed to draw
    u32 layerHits = 0;       // LayerRef rasters reused
    u32 layerMisses = 0;     // LayerRef rasters (re)created
    u32 layerInline = 0;     // LayerRefs too large to cache, drawn directly
    u32 offscreenLayers = 0; // SaveLayers with blur or tint, drawn through an offscreen target
    u32 textLayouts = 0;     // text layouts created (not from the cache)
    u64 layerCacheBytes = 0;
    u64 imageCacheBytes = 0;
};

// The presented frame read back from the GPU: premultiplied BGRA, top row first.
struct FrameCapture
{
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> bgra;
};

// Writes a capture as a PNG file (un-premultiplied RGBA).
[[nodiscard]] bool saveCapturePng(const FrameCapture& capture, const std::string& path);

// The graphics device: D3D11 + DXGI + Direct2D (+ WIC for images) on Windows. A hardware
// device is preferred; WARP is used when no hardware device can be created.
class GraphicsDevice final
{
public:
    GraphicsDevice();
    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    void shutdown() noexcept;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] GraphicsBackend getBackend() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;
    [[nodiscard]] const std::string& getAdapterName() const noexcept;
    [[nodiscard]] bool isSoftware() const noexcept;
    // Incremented whenever the device is recreated after a loss; surfaces follow it.
    [[nodiscard]] u64 getGeneration() const noexcept;
    // Test hook: treats the device as lost, as DXGI_ERROR_DEVICE_REMOVED would, and recreates it.
    [[nodiscard]] bool simulateDeviceLoss() noexcept;

    struct Impl;

private:
    friend class GraphicsSurface;
    std::unique_ptr<Impl> m_impl;
};

// A window's swap chain with a Direct2D target. draw() records a display list (in DIPs) with
// BeginDraw/EndDraw and presents it with vsync. Device loss is recovered transparently
// (the frame is drawn again on the recreated device).
class GraphicsSurface final
{
public:
    GraphicsSurface();
    ~GraphicsSurface();

    GraphicsSurface(const GraphicsSurface&) = delete;
    GraphicsSurface& operator=(const GraphicsSurface&) = delete;

    // nativeWindow is an HWND on Windows. It is borrowed, not owned.
    [[nodiscard]] bool attach(GraphicsDevice& device, void* nativeWindow,
                              const SurfaceConfig& config) noexcept;
    [[nodiscard]] bool resize(const SurfaceConfig& config) noexcept;
    // Returns false when the frame could not be presented, or some command could not be drawn
    // (the rest of the frame is still presented); lastError() and getLastStats() tell which.
    [[nodiscard]] bool draw(const DisplayList& displayList, FrameCapture* capture = nullptr) noexcept;
    [[nodiscard]] bool isAttached() const noexcept;
    void detach() noexcept;

    [[nodiscard]] const SurfaceConfig& getConfig() const noexcept;
    [[nodiscard]] const DrawStats& getLastStats() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;
    [[nodiscard]] u64 getPresentCount() const noexcept;
    [[nodiscard]] u64 getDeviceRecoveries() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace velora
