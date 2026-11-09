// DisplayList playback on a Direct2D device context.
//
// State follows the display list's stack: Save/Transform/SaveLayer push a frame, Restore pops
// it together with every clip and layer pushed in it. Opacity multiplies the frame's opacity;
// inside a SaveLayer it restarts at 1 and the group is composited with the layer's opacity.
// A SaveLayer with blur or tint is drawn through an offscreen bitmap and D2D effects.
#include "velora/graphics/windows/D2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <d2d1effects.h>

namespace velora
{

namespace
{
using Matrix = D2D1::Matrix3x2F;

constexpr f32 kEpsilon = 1e-4f;

D2D1_COLOR_F color(const Color& c, f32 opacity)
{
    return D2D1::ColorF(c.r, c.g, c.b, c.a * opacity);
}

D2D1_RECT_F rect(const Rect& r)
{
    return D2D1::RectF(r.x, r.y, r.x + r.width, r.y + r.height);
}

bool axisAligned(const D2D1_MATRIX_3X2_F& m)
{
    return std::abs(m._12) < kEpsilon && std::abs(m._21) < kEpsilon;
}

// The larger of the matrix's axis scales: how many target pixels one unit covers at most.
f32 matrixScale(const D2D1_MATRIX_3X2_F& m)
{
    return std::sqrt(std::max(m._11 * m._11 + m._12 * m._12, m._21 * m._21 + m._22 * m._22));
}

// Snaps an axis-aligned rectangle to device pixels so edges are crisp: fills to whole pixels,
// strokes of an odd pixel width to pixel centres. Rotated or skewed rectangles are left alone.
D2D1_RECT_F snap(const D2D1_RECT_F& r, const D2D1_MATRIX_3X2_F& m, f32 strokePixels)
{
    if (!axisAligned(m) || std::abs(m._11) < kEpsilon || std::abs(m._22) < kEpsilon)
        return r;
    const f32 width = std::round(strokePixels);
    const f32 offset = strokePixels > 0 && std::fmod(std::max(width, 1.0f), 2.0f) == 1.0f ? 0.5f : 0.0f;
    const auto snapX = [&](f32 x)
    { return (std::round(x * m._11 + m._31 - offset) + offset - m._31) / m._11; };
    const auto snapY = [&](f32 y)
    { return (std::round(y * m._22 + m._32 - offset) + offset - m._32) / m._22; };
    return D2D1::RectF(snapX(r.left), snapY(r.top), snapX(r.right), snapY(r.bottom));
}

// The axis-aligned pixel box of `r` under `m`, clamped to the target.
D2D1_RECT_L deviceBox(const D2D1_RECT_F& r, const D2D1_MATRIX_3X2_F& m, D2D1_SIZE_U target)
{
    const D2D1_POINT_2F corners[] = {
        {r.left, r.top}, {r.right, r.top}, {r.left, r.bottom}, {r.right, r.bottom}};
    f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const auto& c : corners)
    {
        const D2D1_POINT_2F p = Matrix::ReinterpretBaseType(&m)->TransformPoint(c);
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    const auto clampTo = [](f32 v, u32 limit) { return static_cast<LONG>(std::clamp(v, 0.0f, f32(limit))); };
    return {clampTo(std::floor(x0), target.width), clampTo(std::floor(y0), target.height),
            clampTo(std::ceil(x1), target.width), clampTo(std::ceil(y1), target.height)};
}

const char* featureName(UnsupportedFeature feature)
{
    switch (feature)
    {
    case UnsupportedFeature::kImage:
        return "image";
    case UnsupportedFeature::kGlyphRun:
        return "glyph run";
    case UnsupportedFeature::kShadow:
        return "shadow";
    case UnsupportedFeature::kBackdropFilter:
        return "backdrop filter";
    }
    return "unknown";
}

bool opensFrame(const Command& c)
{
    return std::holds_alternative<SaveCommand>(c) || std::holds_alternative<SaveLayerCommand>(c) ||
           std::holds_alternative<TransformCommand>(c);
}

// The Restore matching the frame opened at `open`, or `end` when there is none.
const Command* matchingRestore(const Command* open, const Command* end)
{
    int depth = 0;
    for (const Command* it = open; it != end; ++it)
    {
        if (opensFrame(*it))
            ++depth;
        else if (std::holds_alternative<RestoreCommand>(*it) && --depth == 0)
            return it;
    }
    return end;
}

class Player
{
public:
    Player(GraphicsSurface::Impl& surface, ID2D1DeviceContext* dc, u32 depth)
        : m_surface(surface), m_dc(dc), m_depth(depth), m_targetSize(dc->GetPixelSize())
    {
        dc->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0), &m_brush);
    }

    void play(const Command* begin, const Command* end, const D2D1_MATRIX_3X2_F& base, f32 opacity,
              bool grayscale)
    {
        m_frames.push_back({base, opacity, grayscale, {}});
        for (const Command* it = begin; it != end; ++it)
            it = step(it, end);
        if (m_frames.size() > 1)
        {
            char message[96];
            std::snprintf(message, sizeof message, "display list is unbalanced: %zu frame(s) not restored",
                          m_frames.size() - 1);
            m_surface.fail(message);
        }
        while (!m_frames.empty())
            popFrame();
    }

private:
    enum class Pushed : u8
    {
        kClip,
        kLayer
    };
    struct Frame
    {
        D2D1_MATRIX_3X2_F matrix;
        f32 opacity;
        bool grayscale;
        std::vector<Pushed> pushed;
    };

    Frame& top()
    {
        return m_frames.back();
    }

    void popFrame()
    {
        for (auto it = top().pushed.rbegin(); it != top().pushed.rend(); ++it)
        {
            if (*it == Pushed::kClip)
                m_dc->PopAxisAlignedClip();
            else
                m_dc->PopLayer();
        }
        m_frames.pop_back();
    }

    void pushFrame(const D2D1_MATRIX_3X2_F& matrix, f32 opacity, bool grayscale)
    {
        m_frames.push_back({matrix, opacity, grayscale, {}});
    }

    ID2D1SolidColorBrush* brush(const Color& c)
    {
        m_brush->SetColor(color(c, top().opacity));
        return m_brush.Get();
    }

    void useTransform()
    {
        m_dc->SetTransform(top().matrix);
    }

    // Returns the last command consumed (an offscreen SaveLayer consumes through its Restore).
    const Command* step(const Command* it, const Command* end)
    {
        std::visit([&](const auto& c) { it = draw(c, it, end); }, *it);
        return it;
    }

    const Command* draw(const ClearCommand& c, const Command* it, const Command*)
    {
        m_dc->Clear(color(c.color, top().opacity));
        return it;
    }

    const Command* draw(const RectCommand& c, const Command* it, const Command*)
    {
        useTransform();
        const Frame& f = top();
        if (c.paint.stroke)
        {
            const f32 width = std::max(c.paint.strokeWidth, 0.0f);
            m_dc->DrawRectangle(snap(rect(c.bounds), f.matrix, width * matrixScale(f.matrix)),
                                brush(c.paint.color), width);
        }
        else
            m_dc->FillRectangle(snap(rect(c.bounds), f.matrix, 0), brush(c.paint.color));
        return it;
    }

    const Command* draw(const RoundedRectCommand& c, const Command* it, const Command*)
    {
        useTransform();
        const Frame& f = top();
        const f32 width = c.paint.stroke ? std::max(c.paint.strokeWidth, 0.0f) : 0.0f;
        const f32 radius = std::clamp(c.radius, 0.0f, std::min(c.bounds.width, c.bounds.height) / 2);
        const D2D1_ROUNDED_RECT shape{snap(rect(c.bounds), f.matrix, width * matrixScale(f.matrix)), radius,
                                      radius};
        if (c.paint.stroke)
            m_dc->DrawRoundedRectangle(shape, brush(c.paint.color), width);
        else
            m_dc->FillRoundedRectangle(shape, brush(c.paint.color));
        return it;
    }

    const Command* draw(const PathCommand& c, const Command* it, const Command*)
    {
        ComPtr<ID2D1PathGeometry> geometry;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(m_surface.dev().d2dFactory->CreatePathGeometry(&geometry)) ||
            FAILED(geometry->Open(&sink)))
        {
            m_surface.fail("path geometry could not be created");
            return it;
        }
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        const D2D1_FIGURE_BEGIN begin = c.paint.stroke ? D2D1_FIGURE_BEGIN_HOLLOW : D2D1_FIGURE_BEGIN_FILLED;
        bool open = false;
        const auto ensureOpen = [&](D2D1_POINT_2F at)
        {
            if (!open)
                sink->BeginFigure(at, begin);
            open = true;
        };
        const auto p = [](Point q) { return D2D1::Point2F(q.x, q.y); };
        for (const PathElement& e : c.elements)
        {
            switch (e.verb)
            {
            case PathVerb::kMoveTo:
                if (open)
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                open = false;
                ensureOpen(p(e.p1));
                break;
            case PathVerb::kLineTo:
                ensureOpen({0, 0});
                sink->AddLine(p(e.p1));
                break;
            case PathVerb::kQuadTo:
                ensureOpen({0, 0});
                sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(p(e.p1), p(e.p2)));
                break;
            case PathVerb::kCubicTo:
                ensureOpen({0, 0});
                sink->AddBezier(D2D1::BezierSegment(p(e.p1), p(e.p2), p(e.p3)));
                break;
            case PathVerb::kClose:
                if (open)
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                open = false;
                break;
            }
        }
        if (open)
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
        if (FAILED(sink->Close()))
        {
            m_surface.fail("path geometry is invalid");
            return it;
        }
        useTransform();
        if (c.paint.stroke)
            m_dc->DrawGeometry(geometry.Get(), brush(c.paint.color), std::max(c.paint.strokeWidth, 0.0f));
        else
            m_dc->FillGeometry(geometry.Get(), brush(c.paint.color));
        return it;
    }

    void clipToGeometry(ID2D1Geometry* geometry)
    {
        const D2D1_LAYER_PARAMETERS1 params =
            D2D1::LayerParameters1(D2D1::InfiniteRect(), geometry, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_dc->PushLayer(params, nullptr);
        top().pushed.push_back(Pushed::kLayer);
        top().grayscale = true; // ClearType needs an opaque destination
    }

    void clipToRect(const D2D1_RECT_F& r)
    {
        useTransform();
        if (axisAligned(top().matrix))
        {
            m_dc->PushAxisAlignedClip(snap(r, top().matrix, 0), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            top().pushed.push_back(Pushed::kClip);
            return;
        }
        ComPtr<ID2D1RectangleGeometry> geometry;
        if (FAILED(m_surface.dev().d2dFactory->CreateRectangleGeometry(r, &geometry)))
        {
            m_surface.fail("clip geometry could not be created");
            return;
        }
        clipToGeometry(geometry.Get());
    }

    const Command* draw(const ClipRectCommand& c, const Command* it, const Command*)
    {
        clipToRect(rect(c.bounds));
        return it;
    }

    const Command* draw(const ClipRoundedRectCommand& c, const Command* it, const Command*)
    {
        const f32 radius = std::clamp(c.radius, 0.0f, std::min(c.bounds.width, c.bounds.height) / 2);
        if (radius <= 0)
        {
            clipToRect(rect(c.bounds));
            return it;
        }
        ComPtr<ID2D1RoundedRectangleGeometry> geometry;
        if (FAILED(m_surface.dev().d2dFactory->CreateRoundedRectangleGeometry(
                {snap(rect(c.bounds), top().matrix, 0), radius, radius}, &geometry)))
        {
            m_surface.fail("clip geometry could not be created");
            return it;
        }
        useTransform();
        clipToGeometry(geometry.Get());
        return it;
    }

    const Command* draw(const TransformCommand& c, const Command* it, const Command*)
    {
        const Transform& t = c.transform;
        const Frame f = top();
        pushFrame(Matrix::Rotation(t.rotationDegrees) * Matrix::Scale(t.scaleX, t.scaleY) *
                      Matrix::Translation(t.translateX, t.translateY) *
                      *Matrix::ReinterpretBaseType(&f.matrix),
                  f.opacity, f.grayscale);
        return it;
    }

    const Command* draw(const SaveCommand&, const Command* it, const Command*)
    {
        const Frame f = top();
        pushFrame(f.matrix, f.opacity, f.grayscale);
        return it;
    }

    const Command* draw(const RestoreCommand&, const Command* it, const Command*)
    {
        if (m_frames.size() <= 1)
            m_surface.fail("Restore without a matching Save");
        else
            popFrame();
        return it;
    }

    const Command* draw(const OpacityCommand& c, const Command* it, const Command*)
    {
        top().opacity *= std::clamp(c.value, 0.0f, 1.0f);
        return it;
    }

    const Command* draw(const SaveLayerCommand& c, const Command* it, const Command* end)
    {
        const Frame f = top();
        const f32 opacity = std::clamp(c.opacity, 0.0f, 1.0f) * f.opacity;
        if (c.blurSigma > 0 || c.tint)
            return offscreenLayer(c, opacity, it, end);
        pushFrame(f.matrix, 1.0f, f.grayscale);
        if (opacity < 1.0f) // an opaque group composes like its children drawn directly
        {
            useTransform();
            const D2D1_LAYER_PARAMETERS1 params = D2D1::LayerParameters1(
                rect(c.bounds), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), opacity);
            m_dc->PushLayer(params, nullptr);
            top().pushed.push_back(Pushed::kLayer);
            top().grayscale = true;
        }
        return it;
    }

    // Draws the layer's children into a bitmap with a scratch context, then composites it
    // through GaussianBlur and/or a tint (SourceIn: the tint where the content has alpha).
    const Command* offscreenLayer(const SaveLayerCommand& c, f32 opacity, const Command* it,
                                  const Command* end)
    {
        const Command* close = matchingRestore(it, end);
        if (close == end)
            m_surface.fail("SaveLayer without a matching Restore");
        const Frame f = top();
        const D2D1_RECT_L box = deviceBox(rect(c.bounds), f.matrix, m_targetSize);
        const u32 width = static_cast<u32>(std::max<LONG>(0, box.right - box.left));
        const u32 height = static_cast<u32>(std::max<LONG>(0, box.bottom - box.top));
        if (width == 0 || height == 0 || opacity <= 0)
            return close == end ? end - 1 : close;
        ++m_surface.stats.offscreenLayers;
        ID2D1DeviceContext* scratch = m_surface.scratchContext(m_depth + 1);
        ComPtr<ID2D1Bitmap1> bitmap = scratch ? m_surface.createBitmap(scratch, width, height) : nullptr;
        if (!bitmap)
        {
            m_surface.fail("offscreen layer bitmap could not be created");
            return close == end ? end - 1 : close;
        }
        scratch->SetTarget(bitmap.Get());
        scratch->BeginDraw();
        scratch->Clear(D2D1::ColorF(0, 0, 0, 0));
        const D2D1_MATRIX_3X2_F inner =
            *Matrix::ReinterpretBaseType(&f.matrix) *
            Matrix::Translation(-static_cast<f32>(box.left), -static_cast<f32>(box.top));
        playCommands(m_surface, scratch, m_depth + 1, it + 1, close, inner, 1.0f, true);
        const HRESULT hr = scratch->EndDraw();
        scratch->SetTarget(nullptr);
        if (FAILED(hr))
        {
            m_surface.fail(hresultMessage("offscreen layer", hr));
            return close == end ? end - 1 : close;
        }

        ComPtr<ID2D1Image> output = bitmap;
        ComPtr<ID2D1Effect> blur;
        if (c.blurSigma > 0)
        {
            if (FAILED(m_dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur)))
            {
                m_surface.fail("GaussianBlur effect is unavailable");
                return close == end ? end - 1 : close;
            }
            blur->SetInput(0, output.Get());
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, c.blurSigma * matrixScale(f.matrix));
            blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
            blur->GetOutput(&output);
        }
        ComPtr<ID2D1Effect> tint;
        if (c.tint)
        {
            if (FAILED(m_dc->CreateEffect(CLSID_D2D1ColorMatrix, &tint)))
            {
                m_surface.fail("ColorMatrix effect is unavailable");
                return close == end ? end - 1 : close;
            }
            // Straight-alpha matrix: colour := tint, alpha := alpha * tint alpha.
            const Color& t = *c.tint;
            const D2D1_MATRIX_5X4_F matrix =
                D2D1::Matrix5x4F(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, t.a, t.r, t.g, t.b, 0);
            tint->SetInput(0, output.Get());
            tint->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);
            tint->GetOutput(&output);
        }
        m_dc->SetTransform(D2D1::IdentityMatrix());
        const bool group = opacity < 1.0f;
        if (group)
            m_dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr,
                                                   D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                                   opacity),
                            nullptr);
        const D2D1_POINT_2F origin = D2D1::Point2F(static_cast<f32>(box.left), static_cast<f32>(box.top));
        m_dc->DrawImage(output.Get(), &origin, nullptr, D2D1_INTERPOLATION_MODE_LINEAR);
        if (group)
            m_dc->PopLayer();
        return close == end ? end - 1 : close;
    }

    const Command* draw(const LayerRef& ref, const Command* it, const Command*)
    {
        if (!ref.commands)
        {
            m_surface.fail("LayerRef without commands");
            return it;
        }
        const Frame f = top();
        const auto& body = ref.commands->commands;
        const f32 scale = matrixScale(f.matrix);
        const f32 w = std::ceil(ref.bounds.width * scale);
        const f32 h = std::ceil(ref.bounds.height * scale);
        if (w < 1 || h < 1)
            return it;
        const auto drawInline = [&]
        {
            ++m_surface.stats.layerInline;
            playCommands(m_surface, m_dc, m_depth, body.data(), body.data() + body.size(), f.matrix,
                         f.opacity, f.grayscale);
        };
        if (w > kMaxLayerPixels || h > kMaxLayerPixels || u64(w) * u64(h) * 4 > kLayerCacheBudget)
        {
            drawInline();
            return it;
        }
        LayerRaster& raster = m_surface.layers[ref.id];
        raster.used = m_surface.clock;
        if (raster.bitmap && raster.version == ref.version && std::abs(raster.scale - scale) < kEpsilon)
            ++m_surface.stats.layerHits;
        else
        {
            ++m_surface.stats.layerMisses;
            raster = {ref.version, scale, nullptr, static_cast<u32>(w), static_cast<u32>(h), m_surface.clock};
            ID2D1DeviceContext* scratch = m_surface.scratchContext(m_depth + 1);
            raster.bitmap = scratch ? m_surface.createBitmap(scratch, raster.width, raster.height) : nullptr;
            HRESULT hr = E_FAIL;
            if (raster.bitmap)
            {
                scratch->SetTarget(raster.bitmap.Get());
                scratch->BeginDraw();
                scratch->Clear(D2D1::ColorF(0, 0, 0, 0));
                playCommands(m_surface, scratch, m_depth + 1, body.data(), body.data() + body.size(),
                             Matrix::Translation(-ref.bounds.x, -ref.bounds.y) * Matrix::Scale(scale, scale),
                             1.0f, true);
                hr = scratch->EndDraw();
                scratch->SetTarget(nullptr);
            }
            if (FAILED(hr))
            {
                m_surface.layers.erase(ref.id);
                drawInline();
                return it;
            }
        }
        m_dc->SetTransform(Matrix::Scale(1 / scale, 1 / scale) *
                           Matrix::Translation(ref.bounds.x, ref.bounds.y) *
                           *Matrix::ReinterpretBaseType(&f.matrix));
        m_dc->DrawBitmap(raster.bitmap.Get(), D2D1::RectF(0, 0, w, h), f.opacity,
                         D2D1_INTERPOLATION_MODE_LINEAR, nullptr);
        return it;
    }

    const Command* draw(const TextCommand& c, const Command* it, const Command*)
    {
        if (c.value.empty())
            return it;
        IDWriteTextLayout* layout = m_surface.textLayout(c);
        if (!layout)
        {
            m_surface.fail("DirectWrite text layout could not be created");
            return it;
        }
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        const bool clip = metrics.widthIncludingTrailingWhitespace > c.bounds.width + kEpsilon ||
                          metrics.height > c.bounds.height + kEpsilon;
        if (clip)
        {
            pushFrame(top().matrix, top().opacity, top().grayscale);
            clipToRect(rect(c.bounds));
        }
        useTransform();
        m_dc->SetTextAntialiasMode(top().grayscale ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE
                                                   : D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        m_dc->DrawTextLayout(D2D1::Point2F(c.bounds.x, c.bounds.y), layout, brush(c.paint.color),
                             D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        if (clip)
            popFrame();
        return it;
    }

    const Command* draw(const ImageCommand& c, const Command* it, const Command*)
    {
        ID2D1Bitmap1* bitmap = m_surface.image(m_dc, c.path);
        if (!bitmap)
        {
            m_surface.fail("image could not be decoded: " + c.path);
            return it;
        }
        const D2D1_SIZE_F size = bitmap->GetSize();
        if (size.width <= 0 || size.height <= 0 || c.bounds.width <= 0 || c.bounds.height <= 0)
            return it;
        D2D1_RECT_F dest = rect(c.bounds);
        D2D1_RECT_F source = D2D1::RectF(0, 0, size.width, size.height);
        const f32 sx = c.bounds.width / size.width;
        const f32 sy = c.bounds.height / size.height;
        if (c.fit == ImageFit::kContain)
        {
            const f32 s = std::min(sx, sy);
            const f32 w = size.width * s;
            const f32 h = size.height * s;
            dest = D2D1::RectF(c.bounds.x + (c.bounds.width - w) / 2, c.bounds.y + (c.bounds.height - h) / 2,
                               0, 0);
            dest.right = dest.left + w;
            dest.bottom = dest.top + h;
        }
        else if (c.fit == ImageFit::kCover)
        {
            const f32 s = std::max(sx, sy);
            const f32 w = c.bounds.width / s;
            const f32 h = c.bounds.height / s;
            source = D2D1::RectF((size.width - w) / 2, (size.height - h) / 2, 0, 0);
            source.right = source.left + w;
            source.bottom = source.top + h;
        }
        useTransform();
        m_dc->DrawBitmap(bitmap, dest, top().opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, &source);
        return it;
    }

    const Command* draw(const UnsupportedCommand& c, const Command* it, const Command*)
    {
        m_surface.fail(std::string("unsupported feature: ") + featureName(c.feature));
        return it;
    }

    GraphicsSurface::Impl& m_surface;
    ID2D1DeviceContext* m_dc;
    u32 m_depth;
    D2D1_SIZE_U m_targetSize{};
    ComPtr<ID2D1SolidColorBrush> m_brush;
    std::vector<Frame> m_frames;
};
} // namespace

void playCommands(GraphicsSurface::Impl& surface, ID2D1DeviceContext* dc, u32 depth, const Command* begin,
                  const Command* end, const D2D1_MATRIX_3X2_F& base, f32 opacity, bool grayscaleText)
{
    Player(surface, dc, depth).play(begin, end, base, opacity, grayscaleText);
}

} // namespace velora
