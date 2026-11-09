#pragma once

// Internal to the graphics module: the Direct2D backend's COM state. Never included by
// public headers.
#include <d2d1_3.h>
#include <d3d11_1.h>
#include <dwrite_2.h>
#include <dxgi1_3.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <velora/graphics/DisplayList.hpp>
#include <velora/graphics/Graphics.hpp>
#include <wincodec.h>
#include <wrl/client.h>

namespace velora
{

using Microsoft::WRL::ComPtr;

std::string hresultMessage(const char* what, HRESULT hr);
bool isDeviceLost(HRESULT hr) noexcept;

struct GraphicsDevice::Impl
{
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> d3dContext;
    ComPtr<IDXGIFactory2> dxgi;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2d;
    ComPtr<IWICImagingFactory> wic;
    GraphicsBackend backend = GraphicsBackend::kUnavailable;
    std::string error;
    std::string adapter;
    bool software = false;
    bool comInitialized = false;
    u64 generation = 0;

    bool create();
    void release();
    // Releases and recreates every device object; surfaces notice the new generation.
    bool recreate();
};

// A cached raster of a LayerRef, drawn at the device scale it is used with.
struct LayerRaster
{
    u64 version = 0;
    f32 scale = 0;
    ComPtr<ID2D1Bitmap1> bitmap;
    u32 width = 0;  // pixels
    u32 height = 0; // pixels
    u64 used = 0;
};

struct CachedImage
{
    ComPtr<ID2D1Bitmap1> bitmap;
    u64 bytes = 0;
    u64 used = 0;
};

struct CachedText
{
    ComPtr<IDWriteTextLayout> layout;
    u64 used = 0;
};

inline constexpr u64 kLayerCacheBudget = 64ull << 20;
inline constexpr u64 kImageCacheBudget = 32ull << 20;
inline constexpr std::size_t kTextCacheEntries = 512;
inline constexpr u32 kMaxLayerPixels = 4096; // a LayerRef larger than this on a side is drawn inline

struct GraphicsSurface::Impl
{
    GraphicsDevice::Impl* device = nullptr; // borrowed from the GraphicsDevice attached to
    HWND hwnd = nullptr;
    SurfaceConfig config;
    u64 generation = 0;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1Bitmap1> target;
    // Offscreen layers and LayerRef rasters are drawn with their own contexts (one per nesting
    // depth), so the clips and layers pushed on the outer target stay untouched.
    std::vector<ComPtr<ID2D1DeviceContext>> scratch;
    std::unordered_map<u64, LayerRaster> layers;
    std::unordered_map<std::string, CachedImage> images;
    std::unordered_map<std::string, CachedText> texts;
    u64 clock = 0; // frame number, for LRU
    DrawStats stats;
    std::string error;
    u64 presents = 0;
    u64 recoveries = 0;

    GraphicsDevice::Impl& dev() const noexcept
    {
        return *device;
    }
    // Swap chain, context and target for the device's current generation.
    HRESULT createDeviceResources();
    HRESULT createTarget();
    void releaseDeviceResources();
    // Draws and presents one frame; returns the HRESULT that stopped it (S_OK on success).
    // `complete` is cleared when some command could not be drawn.
    HRESULT render(const DisplayList& list, FrameCapture* capture, bool& complete);
    HRESULT readBack(FrameCapture& capture);
    // Records a command the backend could not draw; the first message of a frame is kept.
    void fail(std::string message);

    ID2D1DeviceContext* scratchContext(u32 depth);
    ComPtr<ID2D1Bitmap1> createBitmap(ID2D1DeviceContext* dc, u32 width, u32 height);

    // Caches (d2d_resources.cpp).
    ID2D1Bitmap1* image(ID2D1DeviceContext* dc, const std::string& path);
    IDWriteTextLayout* textLayout(const TextCommand& text);
    u64 layerBytes() const noexcept;
    u64 imageBytes() const noexcept;
    void trimCaches();
};

// Plays commands onto `dc`, which is between BeginDraw and EndDraw (d2d_playback.cpp).
// `base` maps the commands' coordinates to the target's pixels; `depth` is 0 for the
// swap chain target and selects the scratch context for nested offscreen drawing.
// `grayscaleText` is set where ClearType cannot be used (layers, offscreen targets).
void playCommands(GraphicsSurface::Impl& surface, ID2D1DeviceContext* dc, u32 depth, const Command* begin,
                  const Command* end, const D2D1_MATRIX_3X2_F& base, f32 opacity, bool grayscaleText);

} // namespace velora
