// A window's flip-model swap chain with a Direct2D device context drawing into its back buffer.
#include "velora/graphics/windows/D2D.hpp"

#include <cstring>

namespace velora
{

HRESULT GraphicsSurface::Impl::createDeviceResources()
{
    releaseDeviceResources();
    GraphicsDevice::Impl& d = dev();
    if (!d.d2d)
    {
        error = "graphics device is not ready";
        return E_FAIL;
    }
    generation = d.generation;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = config.size.width;
    desc.Height = config.size.height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc = {1, 0};
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    HRESULT hr = d.dxgi->CreateSwapChainForHwnd(d.d3d.Get(), hwnd, &desc, nullptr, nullptr, &swapChain);
    if (FAILED(hr))
    {
        error = hresultMessage("CreateSwapChainForHwnd", hr);
        return hr;
    }
    d.dxgi->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr = d.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)))
    {
        error = hresultMessage("ID2D1Device::CreateDeviceContext", hr);
        return hr;
    }
    return createTarget();
}

HRESULT GraphicsSurface::Impl::createTarget()
{
    ComPtr<IDXGISurface> buffer;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (SUCCEEDED(hr))
        hr = context->CreateBitmapFromDxgiSurface(buffer.Get(), &props, &target);
    if (FAILED(hr))
    {
        error = hresultMessage("swap chain target bitmap", hr);
        return hr;
    }
    context->SetTarget(target.Get());
    return S_OK;
}

void GraphicsSurface::Impl::releaseDeviceResources()
{
    if (context)
        context->SetTarget(nullptr);
    target.Reset();
    context.Reset();
    scratch.clear();
    layers.clear();
    images.clear();
    swapChain.Reset();
    // A flip-model swap chain is destroyed lazily; flush so the window can take a new one.
    if (device && dev().d3dContext)
    {
        dev().d3dContext->ClearState();
        dev().d3dContext->Flush();
    }
}

ID2D1DeviceContext* GraphicsSurface::Impl::scratchContext(u32 depth)
{
    if (scratch.size() < depth)
        scratch.resize(depth);
    ComPtr<ID2D1DeviceContext>& dc = scratch[depth - 1];
    if (!dc && FAILED(dev().d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)))
        return nullptr;
    return dc.Get();
}

ComPtr<ID2D1Bitmap1> GraphicsSurface::Impl::createBitmap(ID2D1DeviceContext* dc, u32 width, u32 height)
{
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ComPtr<ID2D1Bitmap1> bitmap;
    if (FAILED(dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, props, &bitmap)))
        return nullptr;
    return bitmap;
}

void GraphicsSurface::Impl::fail(std::string message)
{
    ++stats.unsupported;
    if (error.empty())
        error = std::move(message);
}

HRESULT GraphicsSurface::Impl::render(const DisplayList& list, FrameCapture* capture, bool& complete)
{
    if (!context || generation != dev().generation)
    {
        const bool recovering = context != nullptr;
        if (const HRESULT hr = createDeviceResources(); FAILED(hr))
            return hr;
        if (recovering)
            ++recoveries; // another surface (or the test hook) recreated the device
    }
    ++clock;
    stats = {};
    error.clear();
    const auto& commands = list.commands();
    stats.commands = static_cast<u32>(commands.size());

    context->BeginDraw();
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    const f32 scale = config.dpiScale;
    playCommands(*this, context.Get(), 0, commands.data(), commands.data() + commands.size(),
                 D2D1::Matrix3x2F::Scale(scale, scale), 1.0f, false);
    HRESULT hr = context->EndDraw();
    if (FAILED(hr))
        return hr;
    if (capture && FAILED(hr = readBack(*capture)))
        return hr;
    hr = swapChain->Present(1, 0); // DXGI_STATUS_OCCLUDED is a success code: nothing to show
    if (FAILED(hr))
        return hr;
    ++presents;
    trimCaches();
    stats.layerCacheBytes = layerBytes();
    stats.imageCacheBytes = imageBytes();
    complete = stats.unsupported == 0;
    return S_OK;
}

HRESULT GraphicsSurface::Impl::readBack(FrameCapture& capture)
{
    GraphicsDevice::Impl& d = dev();
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr))
        return hr;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(hr = d.d3d->CreateTexture2D(&desc, nullptr, &staging)))
        return hr;
    d.d3dContext->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(hr = d.d3dContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return hr;
    capture.width = desc.Width;
    capture.height = desc.Height;
    capture.bgra.resize(std::size_t{desc.Width} * desc.Height * 4);
    for (u32 y = 0; y < desc.Height; ++y)
        std::memcpy(capture.bgra.data() + std::size_t{y} * desc.Width * 4,
                    static_cast<const u8*>(mapped.pData) + std::size_t{y} * mapped.RowPitch, desc.Width * 4);
    d.d3dContext->Unmap(staging.Get(), 0);
    return S_OK;
}

GraphicsSurface::GraphicsSurface() : m_impl(std::make_unique<Impl>()) {}

GraphicsSurface::~GraphicsSurface()
{
    detach();
}

bool GraphicsSurface::attach(GraphicsDevice& device, void* nativeWindow, const SurfaceConfig& config) noexcept
{
    detach();
    Impl& s = *m_impl;
    if (!device.isReady())
        s.error = "graphics device is not ready";
    else if (!nativeWindow)
        s.error = "no native window";
    else if (config.transparent)
        s.error = "transparent surfaces need DirectComposition, which this backend does not use";
    else if (config.size.width == 0 || config.size.height == 0 || config.dpiScale <= 0)
        s.error = "surface needs a non-zero size and a positive DPI scale";
    else
    {
        s.device = device.m_impl.get();
        s.hwnd = static_cast<HWND>(nativeWindow);
        s.config = config;
        if (SUCCEEDED(s.createDeviceResources()))
        {
            s.error.clear();
            return true;
        }
        const std::string error = s.error;
        detach();
        s.error = error;
    }
    return false;
}

bool GraphicsSurface::resize(const SurfaceConfig& config) noexcept
{
    Impl& s = *m_impl;
    if (!isAttached())
    {
        s.error = "surface is not attached";
        return false;
    }
    if (config.transparent || config.dpiScale <= 0)
    {
        s.error = "unsupported surface configuration";
        return false;
    }
    const bool sameSize =
        config.size.width == s.config.size.width && config.size.height == s.config.size.height;
    s.config = config;
    if (sameSize || config.size.width == 0 || config.size.height == 0)
        return true; // a zero-size (minimized) surface keeps its buffers; draw() refuses it
    if (s.generation != s.dev().generation)
        return SUCCEEDED(s.createDeviceResources());
    s.context->SetTarget(nullptr);
    s.target.Reset();
    HRESULT hr = s.swapChain->ResizeBuffers(0, config.size.width, config.size.height, DXGI_FORMAT_UNKNOWN, 0);
    if (isDeviceLost(hr) && s.dev().recreate())
    {
        ++s.recoveries;
        return SUCCEEDED(s.createDeviceResources());
    }
    if (FAILED(hr))
    {
        s.error = hresultMessage("IDXGISwapChain::ResizeBuffers", hr);
        return false;
    }
    return SUCCEEDED(s.createTarget());
}

bool GraphicsSurface::draw(const DisplayList& displayList, FrameCapture* capture) noexcept
{
    Impl& s = *m_impl;
    if (!isAttached())
    {
        s.error = "surface is not attached";
        return false;
    }
    if (s.config.size.width == 0 || s.config.size.height == 0)
    {
        s.error = "surface has zero size";
        return false;
    }
    bool complete = false;
    HRESULT hr = s.render(displayList, capture, complete);
    if (isDeviceLost(hr))
    {
        // Recreate the device unless another surface already has, then draw the frame again.
        if (s.generation == s.dev().generation && !s.dev().recreate())
        {
            s.error = "device lost and could not be recreated: " + s.dev().error;
            return false;
        }
        ++s.recoveries;
        s.context.Reset(); // render() rebuilds everything for the new generation
        hr = s.render(displayList, capture, complete);
    }
    if (FAILED(hr))
    {
        if (s.error.empty())
            s.error = hresultMessage("draw", hr);
        return false;
    }
    return complete;
}

bool GraphicsSurface::isAttached() const noexcept
{
    return m_impl->device && m_impl->swapChain;
}

void GraphicsSurface::detach() noexcept
{
    m_impl->releaseDeviceResources();
    m_impl->texts.clear();
    m_impl->device = nullptr;
    m_impl->hwnd = nullptr;
}

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
    return m_impl->error;
}

u64 GraphicsSurface::getPresentCount() const noexcept
{
    return m_impl->presents;
}

u64 GraphicsSurface::getDeviceRecoveries() const noexcept
{
    return m_impl->recoveries;
}

} // namespace velora
