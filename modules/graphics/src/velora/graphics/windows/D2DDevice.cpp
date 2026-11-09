// The Direct2D graphics device: D3D11 (hardware, else WARP) + DXGI + D2D + WIC.
#include "velora/graphics/windows/D2D.hpp"

#include <algorithm>
#include <cstdio>

namespace velora
{

std::string hresultMessage(const char* what, HRESULT hr)
{
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, "%s failed (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
    return buffer;
}

bool isDeviceLost(HRESULT hr) noexcept
{
    return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

namespace
{
std::string narrow(const wchar_t* text)
{
    const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(count > 0 ? static_cast<std::size_t>(count - 1) : 0, '\0');
    if (count > 1)
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), count, nullptr, nullptr);
    return out;
}

HRESULT createD3D(D3D_DRIVER_TYPE type, ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context)
{
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                                    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
                                                    D3D_FEATURE_LEVEL_9_3};
    return D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, kLevels,
                             static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device, nullptr,
                             &context);
}
} // namespace

bool GraphicsDevice::Impl::create()
{
    if (!comInitialized)
    {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInitialized = SUCCEEDED(co); // RPC_E_CHANGED_MODE: COM is already up in another mode
    }
    HRESULT hr = createD3D(D3D_DRIVER_TYPE_HARDWARE, d3d, d3dContext);
    software = false;
    if (FAILED(hr))
    {
        hr = createD3D(D3D_DRIVER_TYPE_WARP, d3d, d3dContext);
        software = true;
    }
    if (FAILED(hr))
    {
        error = hresultMessage("D3D11CreateDevice (hardware and WARP)", hr);
        return false;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> dxgiAdapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(hr = d3d.As(&dxgiDevice)) || FAILED(hr = dxgiDevice->GetAdapter(&dxgiAdapter)) ||
        FAILED(hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgi))))
    {
        error = hresultMessage("DXGI adapter/factory query", hr);
        return false;
    }
    if (SUCCEEDED(dxgiAdapter->GetDesc(&desc)))
        adapter = narrow(desc.Description);
    D2D1_FACTORY_OPTIONS options{};
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                      reinterpret_cast<void**>(d2dFactory.GetAddressOf()))))
    {
        error = hresultMessage("D2D1CreateFactory", hr);
        return false;
    }
    if (FAILED(hr = d2dFactory->CreateDevice(dxgiDevice.Get(), &d2d)))
    {
        error = hresultMessage("ID2D1Factory1::CreateDevice", hr);
        return false;
    }
    if (FAILED(hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&wic))))
    {
        error = hresultMessage("WIC imaging factory", hr);
        return false;
    }
    backend = GraphicsBackend::kDirect2D;
    error.clear();
    return true;
}

void GraphicsDevice::Impl::release()
{
    wic.Reset();
    d2d.Reset();
    d2dFactory.Reset();
    dxgi.Reset();
    if (d3dContext)
        d3dContext->ClearState();
    d3dContext.Reset();
    d3d.Reset();
    backend = GraphicsBackend::kUnavailable;
}

bool GraphicsDevice::Impl::recreate()
{
    release();
    ++generation;
    return create();
}

GraphicsDevice::GraphicsDevice() : m_impl(std::make_unique<Impl>()) {}

GraphicsDevice::~GraphicsDevice()
{
    shutdown();
}

bool GraphicsDevice::initialize() noexcept
{
    if (isReady())
        return true;
    if (m_impl->create())
        return true;
    m_impl->release();
    return false;
}

void GraphicsDevice::shutdown() noexcept
{
    m_impl->release();
    if (m_impl->comInitialized)
        CoUninitialize();
    m_impl->comInitialized = false;
}

bool GraphicsDevice::isReady() const noexcept
{
    return m_impl->backend != GraphicsBackend::kUnavailable;
}

GraphicsBackend GraphicsDevice::getBackend() const noexcept
{
    return m_impl->backend;
}

const std::string& GraphicsDevice::lastError() const noexcept
{
    return m_impl->error;
}

const std::string& GraphicsDevice::getAdapterName() const noexcept
{
    return m_impl->adapter;
}

bool GraphicsDevice::isSoftware() const noexcept
{
    return m_impl->software;
}

u64 GraphicsDevice::getGeneration() const noexcept
{
    return m_impl->generation;
}

bool GraphicsDevice::simulateDeviceLoss() noexcept
{
    return isReady() && m_impl->recreate();
}

bool saveCapturePng(const FrameCapture& capture, const std::string& path)
{
    if (capture.width == 0 || capture.height == 0 ||
        capture.bgra.size() != std::size_t{capture.width} * capture.height * 4)
        return false;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    {
        std::vector<u8> pixels = capture.bgra;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            const u32 a = pixels[i + 3];
            if (a != 0 && a != 255)
                for (std::size_t c = 0; c < 3; ++c)
                    pixels[i + c] = static_cast<u8>(std::min<u32>(255, (pixels[i + c] * 255u + a / 2) / a));
        }
        const int count = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring wide(count > 0 ? static_cast<std::size_t>(count - 1) : 0, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), count);

        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(&wic))) &&
             SUCCEEDED(wic->CreateStream(&stream)) &&
             SUCCEEDED(stream->InitializeFromFilename(wide.c_str(), GENERIC_WRITE)) &&
             SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
             SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
             SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
             SUCCEEDED(frame->SetSize(capture.width, capture.height)) &&
             SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat32bppBGRA &&
             SUCCEEDED(frame->WritePixels(capture.height, capture.width * 4, static_cast<UINT>(pixels.size()),
                                          pixels.data())) &&
             SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    }
    if (SUCCEEDED(co))
        CoUninitialize();
    return ok;
}

} // namespace velora
