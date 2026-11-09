// The surface's bounded caches: decoded images, text layouts and LayerRef rasters.
#include "velora/graphics/windows/D2D.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <velora/text/windows/DWriteText.hpp>

namespace velora
{

namespace
{
constexpr u64 kStaleFrames = 600; // an entry unused for this many frames is dropped regardless of budget

u64 rasterBytes(const LayerRaster& raster)
{
    return u64{raster.width} * raster.height * 4;
}
u64 entryBytes(const CachedImage& image)
{
    return image.bytes;
}

// Drops stale entries, then the least recently used ones until the map fits `budget`.
template <class Map, class Bytes> void evict(Map& map, u64 budget, u64 clock, Bytes bytes)
{
    u64 total = 0;
    for (auto it = map.begin(); it != map.end();)
    {
        if (clock - it->second.used > kStaleFrames)
            it = map.erase(it);
        else
            total += bytes((it++)->second);
    }
    while (total > budget && !map.empty())
    {
        auto oldest = std::min_element(map.begin(), map.end(), [](const auto& a, const auto& b)
                                       { return a.second.used < b.second.used; });
        total -= bytes(oldest->second);
        map.erase(oldest);
    }
}
} // namespace

ID2D1Bitmap1* GraphicsSurface::Impl::image(ID2D1DeviceContext* dc, const std::string& path)
{
    if (const auto it = images.find(path); it != images.end())
    {
        it->second.used = clock;
        return it->second.bitmap.Get(); // null when the file could not be decoded: not retried
    }
    CachedImage& entry = images[path];
    entry.used = clock;
    IWICImagingFactory* wic = dev().wic.Get();
    // The file is read into memory first: a WIC decoder on a file keeps it open (and locked
    // against deletion) for as long as WIC caches it.
    std::vector<u8> bytes;
    if (std::ifstream in(std::filesystem::path(utf8ToWide(path)), std::ios::binary); in)
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT width = 0;
    UINT height = 0;
    // Bitmaps are 96 DPI so a DIP of the bitmap is one of its pixels, whatever the file says.
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (wic && !bytes.empty() && SUCCEEDED(wic->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size()))) &&
        SUCCEEDED(
            wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                        nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&width, &height)) &&
        SUCCEEDED(dc->CreateBitmapFromWicBitmap(converter.Get(), &props, &entry.bitmap)))
        entry.bytes = u64{width} * height * 4;
    return entry.bitmap.Get();
}

IDWriteTextLayout* GraphicsSurface::Impl::textLayout(const TextCommand& text)
{
    // Layouts do not wrap and are leading-aligned, so the box width does not change the drawing.
    std::string key = std::to_string(text.fontFamily.size()) + ":" + text.fontFamily + text.value;
    key += '\x1f';
    key += std::to_string(text.fontSize);
    if (const auto it = texts.find(key); it != texts.end())
    {
        it->second.used = clock;
        return it->second.layout.Get();
    }
    FontDescriptor font;
    font.family = text.fontFamily.empty() ? "Segoe UI" : text.fontFamily;
    font.sizeDips = text.fontSize;
    CachedText entry{createTextLayout(text.value, font, 0), clock};
    if (!entry.layout)
        return nullptr;
    ++stats.textLayouts;
    return texts.emplace(std::move(key), std::move(entry)).first->second.layout.Get();
}

u64 GraphicsSurface::Impl::layerBytes() const noexcept
{
    u64 total = 0;
    for (const auto& [id, raster] : layers)
        total += rasterBytes(raster);
    return total;
}

u64 GraphicsSurface::Impl::imageBytes() const noexcept
{
    u64 total = 0;
    for (const auto& [path, image] : images)
        total += image.bytes;
    return total;
}

void GraphicsSurface::Impl::trimCaches()
{
    evict(layers, kLayerCacheBudget, clock, rasterBytes);
    evict(images, kImageCacheBudget, clock, entryBytes);
    if (texts.size() > kTextCacheEntries)
    {
        std::vector<std::pair<u64, const std::string*>> order;
        order.reserve(texts.size());
        for (const auto& [key, entry] : texts)
            order.emplace_back(entry.used, &key);
        std::sort(order.begin(), order.end());
        std::vector<std::string> drop;
        for (std::size_t i = 0; i < texts.size() - kTextCacheEntries * 3 / 4; ++i)
            drop.push_back(*order[i].second);
        for (const auto& key : drop)
            texts.erase(key);
    }
}

} // namespace velora
