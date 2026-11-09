// N02 native drawing test: the Direct2D/DirectWrite backend on a real window. Every check reads
// the presented back buffer (FrameCapture) and inspects pixels; screenshots go to argv[1].
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <velora/composition/Compositor.hpp>
#include <velora/graphics/Canvas.hpp>
#include <velora/graphics/Graphics.hpp>
#include <velora/platform/windowing/NativeWindow.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/text/TextService.hpp>
#include <windows.h>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("N02 %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

struct Pixel
{
    int r = 0, g = 0, b = 0;
};

Pixel at(const FrameCapture& c, u32 x, u32 y)
{
    if (x >= c.width || y >= c.height)
        return {-1, -1, -1};
    const u8* p = c.bgra.data() + (std::size_t{y} * c.width + x) * 4;
    return {p[2], p[1], p[0]};
}

bool matches(const Pixel& p, int r, int g, int b, int tolerance = 8)
{
    return std::abs(p.r - r) <= tolerance && std::abs(p.g - g) <= tolerance && std::abs(p.b - b) <= tolerance;
}

// Pixels in the box that differ from white by more than `threshold` on some channel.
int ink(const FrameCapture& c, u32 x0, u32 y0, u32 x1, u32 y1, int threshold = 64)
{
    int count = 0;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x)
        {
            const Pixel p = at(c, x, y);
            if (255 - std::min({p.r, p.g, p.b}) > threshold)
                ++count;
        }
    return count;
}

// Pixels in the box with a clearly coloured (not grey) value: colour emoji glyphs.
int colourful(const FrameCapture& c, u32 x0, u32 y0, u32 x1, u32 y1)
{
    int count = 0;
    for (u32 y = y0; y < y1; ++y)
        for (u32 x = x0; x < x1; ++x)
        {
            const Pixel p = at(c, x, y);
            if (std::max({p.r, p.g, p.b}) - std::min({p.r, p.g, p.b}) > 80)
                ++count;
        }
    return count;
}

constexpr Color kWhite{1, 1, 1, 1};
constexpr Color kRed{1, 0, 0, 1};
constexpr Color kGreen{0, 1, 0, 1};
constexpr Color kBlue{0, 0, 1, 1};
constexpr Color kBlack{0, 0, 0, 1};

Paint fill(Color c)
{
    return {c, 0, false};
}
Paint stroke(Color c, float width)
{
    return {c, width, true};
}

SurfaceConfig config(u32 widthDips, u32 heightDips, f32 scale)
{
    return {
        {static_cast<u32>(std::lround(widthDips * scale)), static_cast<u32>(std::lround(heightDips * scale))},
        scale,
        false};
}

std::string g_out;
void save(const FrameCapture& capture, const char* name)
{
    if (!g_out.empty() && !saveCapturePng(capture, g_out + "/" + name))
        check(false, (std::string("screenshot ") + name).c_str());
}

double cpuSeconds()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    const auto value = [](FILETIME f) { return (u64{f.dwHighDateTime} << 32 | f.dwLowDateTime) / 1e7; };
    return value(kernel) + value(user);
}

void primitives(GraphicsSurface& surface)
{
    Canvas canvas;
    canvas.clear(kWhite);
    canvas.drawRect({10, 10, 40, 30}, fill(kRed));
    canvas.drawRect({60, 10, 40, 30}, stroke(kBlue, 1));
    canvas.drawRoundedRect({110, 10, 60, 30}, 12, fill(kGreen));
    canvas.drawPath({{PathVerb::kMoveTo, {10, 60}},
                     {PathVerb::kLineTo, {50, 60}},
                     {PathVerb::kLineTo, {10, 100}},
                     {PathVerb::kClose}},
                    fill(kBlack));
    canvas.drawPath({{PathVerb::kMoveTo, {60, 100}}, {PathVerb::kCubicTo, {60, 60}, {100, 60}, {100, 100}}},
                    stroke(kBlue, 2));
    FrameCapture capture;
    const bool drawn = surface.draw(std::move(canvas).finish(), &capture);
    check(drawn && capture.width == 200 && capture.height == 150, "frame presented and read back");
    check(matches(at(capture, 30, 25), 255, 0, 0) && matches(at(capture, 9, 25), 255, 255, 255) &&
              matches(at(capture, 10, 25), 255, 0, 0) && matches(at(capture, 49, 39), 255, 0, 0) &&
              matches(at(capture, 50, 40), 255, 255, 255),
          "filled rect edges on whole pixels");
    check(matches(at(capture, 60, 25), 0, 0, 255) && matches(at(capture, 61, 25), 255, 255, 255) &&
              matches(at(capture, 80, 10), 0, 0, 255) && matches(at(capture, 80, 11), 255, 255, 255),
          "1px stroke covers exactly one pixel");
    const Pixel corner = at(capture, 111, 11);
    check(matches(at(capture, 140, 25), 0, 255, 0) && matches(corner, 255, 255, 255, 40),
          "rounded rect corners are cut");
    check(matches(at(capture, 15, 65), 0, 0, 0) && matches(at(capture, 45, 95), 255, 255, 255) &&
              ink(capture, 60, 60, 101, 101) > 40,
          "filled and stroked paths");
    save(capture, "n02-primitives.png");
}

void stateStack(GraphicsSurface& surface)
{
    Canvas canvas;
    canvas.clear(kWhite);
    // Clip, restored afterwards.
    canvas.save();
    canvas.clip({10, 10, 20, 20});
    canvas.drawRect({0, 0, 100, 60}, fill(kBlue));
    canvas.restore();
    canvas.drawRect({40, 40, 10, 10}, fill(kRed));
    // Transform: translate then scale 2.
    canvas.transform({100, 10, 2, 2, 0});
    canvas.drawRect({0, 0, 10, 10}, fill(kGreen));
    canvas.restore();
    // Rotation by 45 degrees around the origin of the translated frame, with a clip.
    canvas.transform({150, 100, 1, 1, 45});
    canvas.clip({-10, -10, 20, 20});
    canvas.drawRect({-50, -50, 100, 100}, fill(kBlack));
    canvas.restore();
    // Opacity groups: overlapping children compose once; plain opacity does not.
    canvas.saveLayer({10, 80, 60, 30}, 0.5f);
    canvas.drawRect({10, 80, 40, 30}, fill(kRed));
    canvas.drawRect({30, 80, 40, 30}, fill(kRed));
    canvas.restore();
    canvas.save();
    canvas.opacity(0.5f);
    canvas.drawRect({10, 115, 40, 30}, fill(kRed));
    canvas.drawRect({30, 115, 40, 30}, fill(kRed));
    canvas.restore();
    FrameCapture capture;
    check(surface.draw(std::move(canvas).finish(), &capture), "state stack frame drawn");
    check(matches(at(capture, 20, 20), 0, 0, 255) && matches(at(capture, 35, 20), 255, 255, 255) &&
              matches(at(capture, 5, 5), 255, 255, 255) && matches(at(capture, 45, 45), 255, 0, 0),
          "clip applies until restore");
    check(matches(at(capture, 101, 11), 0, 255, 0) && matches(at(capture, 119, 29), 0, 255, 0) &&
              matches(at(capture, 121, 31), 255, 255, 255),
          "translate and scale");
    // The 20x20 clip rotated by 45 degrees is a diamond of half-diagonal 14.1 around (150,100).
    check(matches(at(capture, 150, 88), 0, 0, 0) && matches(at(capture, 140, 90), 255, 255, 255) &&
              matches(at(capture, 162, 100), 0, 0, 0),
          "rotated clip is a geometric mask");
    check(matches(at(capture, 20, 95), 255, 128, 128, 10) && matches(at(capture, 40, 95), 255, 128, 128, 10),
          "opacity group composes overlapping children once");
    check(matches(at(capture, 20, 130), 255, 128, 128, 10) && matches(at(capture, 40, 130), 255, 64, 64, 12),
          "plain opacity blends each child");
    save(capture, "n02-state-stack.png");
}

void effectsAndLayers(GraphicsSurface& surface)
{
    auto body = std::make_shared<CommandList>();
    body->commands.push_back(RectCommand{{120, 20, 40, 40}, fill(kBlue)});
    body->commands.push_back(TextCommand{{120, 60, 60, 20}, "Layer", 14, fill(kBlack)});
    const auto frame = [&](u64 version)
    {
        std::vector<Command> commands;
        commands.push_back(ClearCommand{kWhite});
        commands.push_back(SaveLayerCommand{{0, 0, 100, 100}, 1.0f, 3.0f, Color{0, 0, 0, 1}});
        commands.push_back(RectCommand{{20, 20, 40, 40}, fill(kRed)});
        commands.push_back(RestoreCommand{});
        commands.push_back(LayerRef{7, version, {120, 20, 60, 60}, body});
        return DisplayList(std::move(commands));
    };
    FrameCapture capture;
    const bool drawn = surface.draw(frame(1), &capture);
    const DrawStats first = surface.getLastStats();
    check(drawn && first.offscreenLayers == 1 && matches(at(capture, 40, 40), 0, 0, 0, 12),
          "blur and tint layer drawn offscreen");
    const Pixel edge = at(capture, 19, 40);
    check(edge.r > 20 && edge.r < 235 && edge.r == edge.g && edge.g == edge.b,
          "blur softens the tinted edge");
    check(first.layerMisses == 1 && first.layerHits == 0 && matches(at(capture, 140, 40), 0, 0, 255),
          "LayerRef rasterized");
    check(surface.draw(frame(1)) && surface.getLastStats().layerHits == 1 &&
              surface.getLastStats().layerMisses == 0,
          "LayerRef raster reused");
    check(surface.draw(frame(2)) && surface.getLastStats().layerMisses == 1,
          "new LayerRef version re-rasterized");
    check(surface.getLastStats().layerCacheBytes > 0 && surface.getLastStats().layerCacheBytes <= (64u << 20),
          "layer cache within its budget");
    save(capture, "n02-effects.png");
}

void images(GraphicsSurface& surface, const std::string& dir)
{
    // A 2x2 checker scaled up 20x: smooth interpolation must produce intermediate greys.
    FrameCapture checker{2, 2, {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255}};
    const std::string path = dir + "/n02-checker.png";
    check(saveCapturePng(checker, path), "PNG written through WIC");
    Canvas canvas;
    canvas.clear(Color{1, 0, 1, 1});
    canvas.drawImage({10, 10, 40, 40}, path, ImageFit::kStretch);
    canvas.drawImage({60, 10, 80, 40}, path, ImageFit::kContain);
    canvas.drawImage({150, 10, 40, 20}, path, ImageFit::kCover);
    FrameCapture capture;
    check(surface.draw(std::move(canvas).finish(), &capture), "image frame drawn");
    int greys = 0;
    for (u32 x = 10; x < 50; ++x)
    {
        const Pixel p = at(capture, x, 30);
        greys += p.r > 40 && p.r < 215 && std::abs(p.r - p.g) < 6;
    }
    check(greys >= 6, "scaled image is interpolated, not nearest-neighbour");
    check(matches(at(capture, 65, 30), 255, 0, 255) && !matches(at(capture, 100, 30), 255, 0, 255),
          "contain letterboxes");
    check(!matches(at(capture, 151, 11), 255, 0, 255) && !matches(at(capture, 188, 28), 255, 0, 255),
          "cover fills the box");
    Canvas missing;
    missing.drawImage({0, 0, 10, 10}, dir + "/does-not-exist.png");
    check(!surface.draw(std::move(missing).finish()) && surface.getLastStats().unsupported == 1 &&
              surface.lastError().find("does-not-exist") != std::string::npos,
          "missing image is reported");
    std::error_code error;
    check(std::filesystem::remove(path, error), "decoded image file is not kept open");
}

void text(GraphicsSurface& surface, TextService& service)
{
    check(service.resolveFamily("Segoe UI") == "Segoe UI", "Segoe UI resolved");
    check(service.resolveFamily("No Such Font Family") == "Segoe UI", "missing family falls back");
    TextMetrics latin, cjk;
    FontDescriptor font;
    font.sizeDips = 20;
    check(service.measureText("Velora Hello", font, latin) && latin.widthDips > 60 && latin.widthDips < 200 &&
              std::abs(latin.heightDips - 20 * 1.33f) < 0.5f && latin.baselineDips > 10,
          "Latin metrics");
    check(service.measureText("\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C", font, cjk) &&
              cjk.widthDips > 60 && cjk.widthDips < 100,
          "CJK metrics (UTF-8 to UTF-16, font fallback)");

    Canvas canvas;
    canvas.clear(kWhite);
    canvas.drawText({10, 10, 180, 30}, "Velora Hello", 20, fill(kBlack));
    canvas.drawText({10, 45, 180, 30}, "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C", 20, fill(kBlack));
    canvas.drawText({10, 80, 60, 30}, "\xF0\x9F\x98\x80 \xF0\x9F\x8E\xA8", 20, fill(kBlack));
    canvas.drawText({80, 80, 40, 30}, "Clipped long text", 20, fill(kBlack));
    canvas.saveLayer({0, 115, 200, 35}, 0.75f);
    canvas.drawText({10, 115, 180, 30}, "Grayscale in a layer", 16, fill(kBlack));
    canvas.restore();
    FrameCapture capture;
    check(surface.draw(std::move(canvas).finish(), &capture), "text frame drawn");
    check(ink(capture, 10, 10, 190, 40) > 80, "Latin text rendered");
    check(ink(capture, 10, 45, 190, 75) > 120, "CJK text rendered");
    check(colourful(capture, 10, 80, 70, 110) > 40, "colour emoji rendered");
    check(ink(capture, 80, 80, 120, 110) > 20 && ink(capture, 121, 78, 200, 112) == 0,
          "text clipped to bounds");
    check(ink(capture, 10, 115, 190, 145) > 40, "text inside a layer");
    save(capture, "n02-text.png");
}

void dpi(GraphicsSurface& surface, f32 scale, const char* file)
{
    check(surface.resize(config(200, 150, scale)), "resize for DPI");
    Canvas canvas;
    canvas.clear(kWhite);
    canvas.drawRect({10, 10, 20, 20}, fill(kRed));
    canvas.drawRect({40, 10, 20, 20}, stroke(kBlue, 1));
    canvas.drawText({10, 40, 180, 30}, "DPI \xE4\xB8\xAD\xE6\x96\x87", 16, fill(kBlack));
    FrameCapture capture;
    const bool drawn = surface.draw(std::move(canvas).finish(), &capture);
    const int lo = static_cast<int>(std::lround(10 * scale));
    const int hi = static_cast<int>(std::lround(30 * scale));
    char name[64];
    std::snprintf(name, sizeof name, "DIPs map to device pixels at %.1fx", scale);
    check(drawn && capture.width == u32(std::lround(200 * scale)) &&
              matches(at(capture, lo, lo), 255, 0, 0) &&
              matches(at(capture, lo - 1, lo - 1), 255, 255, 255) &&
              matches(at(capture, hi - 1, hi - 1), 255, 0, 0) && matches(at(capture, hi, hi), 255, 255, 255),
          name);
    std::snprintf(name, sizeof name, "text scales with DPI at %.1fx", scale);
    check(ink(capture, lo, u32(40 * scale), u32(190 * scale), u32(70 * scale)) > int(60 * scale * scale),
          name);
    save(capture, file);
}

void diagnostics(GraphicsSurface& surface, GraphicsDevice& device)
{
    Canvas canvas;
    canvas.clear(kWhite);
    canvas.unsupported(UnsupportedFeature::kBackdropFilter);
    canvas.drawRect({10, 10, 20, 20}, fill(kRed));
    FrameCapture capture;
    check(!surface.draw(std::move(canvas).finish(), &capture) && surface.getLastStats().unsupported == 1 &&
              surface.lastError().find("backdrop filter") != std::string::npos &&
              matches(at(capture, 15, 15), 255, 0, 0),
          "unsupported command reported, rest of the frame drawn");
    Canvas unbalanced;
    unbalanced.save();
    unbalanced.clear(kWhite);
    check(!surface.draw(std::move(unbalanced).finish()) &&
              surface.lastError().find("unbalanced") != std::string::npos,
          "unbalanced display list reported");
    Canvas extra;
    extra.restore();
    check(!surface.draw(std::move(extra).finish()) &&
              surface.lastError().find("Restore") != std::string::npos,
          "restore without save reported");

    const u64 generation = device.getGeneration();
    const u64 recoveries = surface.getDeviceRecoveries();
    check(device.simulateDeviceLoss() && device.getGeneration() == generation + 1, "device recreated");
    Canvas after;
    after.clear(kWhite);
    after.drawRect({10, 10, 20, 20}, fill(kGreen));
    check(surface.draw(std::move(after).finish(), &capture) &&
              surface.getDeviceRecoveries() == recoveries + 1 && matches(at(capture, 15, 15), 0, 255, 0),
          "surface recovers after device loss");
}

void resizeSwapChain(GraphicsSurface& surface)
{
    check(surface.resize(config(320, 240, 1)), "swap chain resized");
    Canvas canvas;
    canvas.clear(kWhite);
    canvas.drawRect({300, 220, 20, 20}, fill(kBlue));
    FrameCapture capture;
    check(surface.draw(std::move(canvas).finish(), &capture) && capture.width == 320 &&
              capture.height == 240 && matches(at(capture, 310, 230), 0, 0, 255),
          "frame fills the resized swap chain");
    check(surface.resize(config(320, 0, 1)), "zero size accepted while minimized");
    Canvas none;
    check(!surface.draw(std::move(none).finish()) &&
              surface.lastError().find("zero size") != std::string::npos,
          "zero-size surface refuses to draw");
    check(surface.resize(config(320, 240, 1)), "restored after zero size");
}

void idle(GraphicsSurface& surface, NativeWindow& window)
{
    const u64 presents = surface.getPresentCount();
    const double cpu = cpuSeconds();
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < until)
        window.waitEvents(50);
    const double used = cpuSeconds() - cpu;
    std::printf("N02 idle: %.1f ms CPU over 500 ms, %llu presents\n", used * 1000,
                static_cast<unsigned long long>(surface.getPresentCount() - presents));
    check(surface.getPresentCount() == presents && used < 0.05, "idle: no presents, minimal CPU");
}

// A built-in-framework page laid out with DirectWrite metrics and presented (first frame).
void firstFrame(GraphicsDevice& device, TextService& service, NativeWindow& window)
{
    const f32 scale = 1.5f;
    GraphicsSurface surface;
    check(surface.attach(device, window.nativeHandle(), config(360, 220, scale)), "page surface attached");
    Compositor compositor;
    compositor.setBackground(kWhite);
    Framework ui(compositor,
                 [&](std::string_view text, float size) { return service.measureWidth(text, size); });
    const auto& p = ui.props();
    const ElementId page = ui.create(ElementKind::kPage);
    const ElementId stack = ui.create(ElementKind::kStackPanel, page);
    ui.set(stack, p.Margin, Thickness{20, 16, 20, 16});
    ui.set(stack, p.Spacing, 10.0);
    const ElementId title = ui.create(ElementKind::kTextBlock, stack);
    ui.set(title, p.Text, "Velora \xE2\x80\x94 Direct2D");
    ui.set(title, p.FontSize, 26.0);
    const ElementId body = ui.create(ElementKind::kTextBlock, stack);
    ui.set(body, p.Text,
           "DirectWrite: \xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C\xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x98\x80");
    const ElementId box = ui.create(ElementKind::kTextBox, stack);
    ui.set(box, p.PlaceholderText, "Name");
    const ElementId button = ui.create(ElementKind::kButton, stack);
    ui.set(button, p.Content, "Save");
    ui.update(360, 220);
    compositor.resize(360, 220, scale);
    const SceneSnapshot scene = compositor.snapshot();
    FrameCapture capture;
    const bool drawn = scene.displayList && surface.draw(*scene.displayList, &capture);
    if (!drawn)
        std::printf("N02 first frame error: %s\n", surface.lastError().c_str());
    const Box t = ui.bounds(title);
    const Box b = ui.bounds(body);
    const auto inkIn = [&](const Box& r)
    {
        return ink(capture, u32(r.x * scale), u32(r.y * scale), u32((r.x + r.width) * scale),
                   u32((r.y + r.height) * scale));
    };
    check(drawn && capture.width == 540 && matches(at(capture, 2, 2), 255, 255, 255) && inkIn(t) > 200 &&
              inkIn(t) < int(t.width * t.height * scale * scale / 2) && inkIn(b) > 150,
          "framework page first frame");
    save(capture, "n02-first-frame.png");
}

// N07.5 ScrollViewer on screen: scrolled rows, clipped overflow and the panning indicator
// drawn over the content.
void scrollViewerFrame(GraphicsDevice& device, NativeWindow& window)
{
    GraphicsSurface surface;
    check(surface.attach(device, window.nativeHandle(), config(300, 200, 1)), "scroll surface attached");
    Compositor compositor;
    compositor.setBackground(kWhite);
    Framework ui(compositor);
    const auto& p = ui.props();
    const ElementId page = ui.create(ElementKind::kPage);
    const ElementId viewer = ui.create(ElementKind::kScrollViewer, page);
    ui.set(viewer, p.Margin, Thickness{20, 20, 0, 0});
    ui.set(viewer, p.Width, 200.0);
    ui.set(viewer, p.Height, 120.0);
    ui.set(viewer, p.HorizontalAlignment, std::string("Left"));
    ui.set(viewer, p.VerticalAlignment, std::string("Top"));
    const ElementId stack = ui.create(ElementKind::kStackPanel, viewer);
    for (const i64 color :
         {i64{0xFFFF0000}, i64{0xFF00FF00}, i64{0xFF0000FF}, i64{0xFFFF0000}, i64{0xFF00FF00}})
    {
        const ElementId row = ui.create(ElementKind::kBorder, stack);
        ui.set(row, p.Height, 60.0);
        ui.set(row, p.Background, PropertyValue(color));
    }
    ui.update(300, 200);
    ui.changeView(viewer, std::nullopt, 60.0); // the green row is now at the top
    ui.update(300, 200);
    compositor.resize(300, 200, 1);
    const SceneSnapshot scene = compositor.snapshot();
    FrameCapture capture;
    const bool drawn = scene.displayList && surface.draw(*scene.displayList, &capture);
    // Rows: green 20..80, blue 80..140 (viewer bottom); the red row below is clipped.
    check(drawn && matches(at(capture, 100, 50), 0, 255, 0) && matches(at(capture, 100, 110), 0, 0, 255) &&
              matches(at(capture, 100, 160), 255, 255, 255) && matches(at(capture, 10, 50), 255, 255, 255),
          "scrolled content is offset and clipped to the viewport");
    // Thumb: 3 px at the right edge, track 116 px, length 46.4 px from local y 25.2.
    const Pixel thumb = at(capture, 216, 60), track = at(capture, 216, 120);
    check(drawn && thumb.g < 200 && thumb.r < 40 && thumb.b < 40 && matches(track, 0, 0, 255),
          "panning indicator drawn over the content at the offset");
    save(capture, "n07-scrollviewer.png");

    // Pointer over the bar: it expands to a light track with arrows and a wider thumb.
    ui.pointerMove(214, 80);
    ui.update(300, 200);
    const SceneSnapshot expanded = compositor.snapshot();
    FrameCapture wide;
    const bool drawnWide = expanded.displayList && surface.draw(*expanded.displayList, &wide);
    // Track at x 208..220 of the viewer (DIPs 20+188..20+200); arrow glyph near the top.
    // The track is nearly opaque, so the blue row under it turns pale.
    const Pixel paleTrack = at(wide, 210, 120), wideThumb = at(wide, 214, 60);
    check(drawnWide && paleTrack.r > 200 && paleTrack.g > 200 && wideThumb.r < 160 && wideThumb.g < 160 &&
              ink(wide, 209, 21, 219, 31) > 4,
          "expanded scroll bar: track, thumb and arrow");
    save(wide, "n07-scrollviewer-expanded.png");
}

} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1)
    {
        g_out = argv[1];
        std::filesystem::create_directories(g_out);
    }
    GraphicsDevice device;
    check(device.initialize() && device.getBackend() == GraphicsBackend::kDirect2D && device.isReady(),
          "Direct2D device initialized");
    std::printf("N02 adapter: %s%s\n", device.getAdapterName().c_str(), device.isSoftware() ? " (WARP)" : "");
    TextService service;
    check(service.initialize() && service.isReady(), "DirectWrite initialized");

    NativeWindow window;
    WindowDesc desc;
    desc.title = "Velora N02 graphics";
    desc.width = 560;
    desc.height = 420;
    check(window.create(desc), "window created");
    window.pump();

    GraphicsSurface surface;
    GraphicsSurface unattached;
    check(!unattached.draw(DisplayList(std::vector<Command>{})) && !unattached.lastError().empty(),
          "unattached surface refuses");
    SurfaceConfig transparent = config(200, 150, 1);
    transparent.transparent = true;
    check(!unattached.attach(device, window.nativeHandle(), transparent) &&
              unattached.lastError().find("DirectComposition") != std::string::npos,
          "transparent surface reported as unsupported");
    check(surface.attach(device, window.nativeHandle(), config(200, 150, 1)), "surface attached");
    primitives(surface);
    stateStack(surface);
    effectsAndLayers(surface);
    images(surface, g_out.empty() ? std::filesystem::temp_directory_path().string() : g_out);
    text(surface, service);
    dpi(surface, 1.0f, "n02-dpi-100.png");
    dpi(surface, 1.5f, "n02-dpi-150.png");
    dpi(surface, 2.0f, "n02-dpi-200.png");
    resizeSwapChain(surface);
    diagnostics(surface, device);
    idle(surface, window);
    surface.detach();
    firstFrame(device, service, window);
    scrollViewerFrame(device, window);

    window.close();
    std::printf("graphics exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
