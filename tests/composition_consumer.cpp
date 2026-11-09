#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>
#include <velora/composition/FrameScheduler.hpp>
#include <velora/graphics/Canvas.hpp>

using namespace velora;
using namespace velora;

namespace
{

struct MockSink final : FrameSink
{
    std::vector<SceneSnapshot> frames;
    std::vector<std::string> captures;
    bool present(const SceneSnapshot& scene, const std::string* capturePath) override
    {
        frames.push_back(scene);
        if (capturePath)
            captures.push_back(*capturePath);
        return true;
    }
};

DisplayList rect(float x, float y, float w, float h, Color color)
{
    Canvas canvas;
    canvas.drawRect({x, y, w, h}, {color});
    return std::move(canvas).finish();
}

// The command stream as the backend sees it, with retained LayerRef subtrees expanded.
void expand(const std::vector<Command>& in, std::vector<Command>& out)
{
    for (const auto& c : in)
    {
        if (const auto* ref = std::get_if<LayerRef>(&c))
            expand(ref->commands->commands, out);
        else
            out.push_back(c);
    }
}

std::vector<Command> flat(const SceneSnapshot& scene)
{
    std::vector<Command> out;
    expand(scene.displayList->commands(), out);
    return out;
}

template <class T> std::vector<const T*> topLevel(const SceneSnapshot& scene)
{
    std::vector<const T*> out;
    for (const auto& c : scene.displayList->commands())
        if (const auto* v = std::get_if<T>(&c))
            out.push_back(v);
    return out;
}

template <class T> std::size_t count(const SceneSnapshot& scene)
{
    std::size_t n = 0;
    for (const auto& c : flat(scene))
        n += std::holds_alternative<T>(c);
    return n;
}

// Index of the first Rectangle with the given red channel, used to check paint order.
std::size_t rectIndex(const SceneSnapshot& scene, float red)
{
    const auto commands = flat(scene);
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        if (const auto* r = std::get_if<RectCommand>(&commands[i]); r && r->paint.color.r == red)
            return i;
    }
    return commands.size();
}

bool balanced(const SceneSnapshot& scene)
{
    int depth = 0;
    for (const auto& c : flat(scene))
    {
        if (std::holds_alternative<SaveCommand>(c) || std::holds_alternative<SaveLayerCommand>(c) ||
            std::holds_alternative<TransformCommand>(c))
            ++depth;
        if (std::holds_alternative<RestoreCommand>(c) && --depth < 0)
            return false;
    }
    return depth == 0;
}

} // namespace

int main()
{
    Compositor compositor(64 * 1024);
    MockSink sink;
    FrameScheduler scheduler(compositor, sink);
    compositor.resize(800, 600, 1);

    // Two cached panels with 50 leaves each, an un-cached overlay, and a nested opacity group.
    const auto panelA = compositor.createVisual(compositor.root());
    const auto panelB = compositor.createVisual(compositor.root());
    compositor.setLayer(panelA, true);
    compositor.setLayer(panelB, true);
    compositor.setOffset(panelB, {400, 0});
    std::vector<VisualId> leavesA;
    for (int i = 0; i < 50; ++i)
    {
        leavesA.push_back(compositor.createVisual(panelA));
        compositor.setContent(leavesA.back(), rect(0, static_cast<float>(i * 10), 100, 8, {0.1f, 0, 0, 1}));
        compositor.setContent(compositor.createVisual(panelB),
                              rect(0, static_cast<float>(i * 10), 100, 8, {0.2f, 0, 0, 1}));
    }

    assert(scheduler.hasPendingWork() && scheduler.tick(0.0));
    const auto& first = sink.frames.back();
    assert(first.width == 800 && first.height == 600 && balanced(first));
    assert(first.stats.layerMisses == 2 && first.stats.layerHits == 0 && first.stats.visualsLowered == 103);
    assert(compositor.layerCache().entries == 2);
    const auto firstRefs = topLevel<LayerRef>(first);
    assert(firstRefs.size() == 2 && firstRefs[0]->bounds.height == 498 &&
           firstRefs[1]->commands->commands.size() == 50);

    // Settled scene: no work, no submission.
    for (int i = 0; i < 100; ++i)
        assert(!scheduler.tick(0.1 + i * 0.016));
    assert(!scheduler.hasPendingWork() && sink.frames.size() == 1 && scheduler.telemetry().idleTicks == 100);

    // Local dirty: one leaf in panel A re-lowers panel A only; panel B's fragment is reused.
    compositor.setContent(leavesA[7], rect(0, 70, 100, 8, {0.3f, 0, 0, 1}));
    assert(scheduler.tick(2.0));
    auto stats = sink.frames.back().stats;
    assert(stats.layerMisses == 1 && stats.layerHits == 1 &&
           stats.visualsLowered == 53); // root, A, 50 leaves, B (hit)
    assert(rectIndex(sink.frames.back(), 0.3f) < flat(sink.frames.back()).size());
    {
        const auto refs = topLevel<LayerRef>(sink.frames.back());
        assert(refs.size() == 2 && refs[0]->version != firstRefs[0]->version); // panel A re-lowered
        assert(refs[1]->version == firstRefs[1]->version &&
               refs[1]->commands == firstRefs[1]->commands); // B reused
    }

    // Render-property change on a cached layer reuses its own fragment.
    compositor.setOpacity(panelB, 0.5f);
    assert(scheduler.tick(2.1));
    stats = sink.frames.back().stats;
    assert(stats.layerHits == 2 && stats.layerMisses == 0 && stats.visualsLowered == 3);
    assert(count<SaveLayerCommand>(sink.frames.back()) == 1 && balanced(sink.frames.back()));
    compositor.setOpacity(panelB, 0.5f); // unchanged value: no frame
    assert(!scheduler.tick(2.2));

    // Nested opacity and clip: outer group 0.5 > inner group 0.5, clipped to 50x50.
    const auto outer = compositor.createVisual(compositor.root());
    const auto inner = compositor.createVisual(outer);
    compositor.setOpacity(outer, 0.5f);
    compositor.setOpacity(inner, 0.5f);
    compositor.setOffset(outer, {10, 400});
    compositor.setClip(inner, {VisualClip::Kind::kRect, {0, 0, 50, 50}});
    compositor.setContent(inner, rect(-20, -20, 200, 200, {0.4f, 0, 0, 1}));
    assert(scheduler.tick(3.0));
    const auto& nested = sink.frames.back();
    std::vector<const SaveLayerCommand*> layers;
    const ClipRectCommand* clip = nullptr;
    const auto nestedCommands = flat(nested);
    for (const auto& c : nestedCommands)
    {
        if (const auto* l = std::get_if<SaveLayerCommand>(&c))
            layers.push_back(l);
        if (const auto* cl = std::get_if<ClipRectCommand>(&c))
            clip = cl;
    }
    assert(layers.size() == 3 && layers[1]->opacity == 0.5f && layers[2]->opacity == 0.5f);
    assert(clip && clip->bounds.width == 50);
    // Inner group bounds are its content intersected with the clip; the outer group sees the same box.
    assert(layers[2]->bounds.x == 0 && layers[2]->bounds.width == 50 && layers[1]->bounds.width == 50);
    assert(balanced(nested));

    // Overlay z-order: created first with a higher z-index, painted after its later sibling.
    const auto overlay = compositor.createVisual(compositor.root());
    const auto sibling = compositor.createVisual(compositor.root());
    compositor.setContent(overlay, rect(100, 100, 50, 50, {0.9f, 0, 0, 1}));
    compositor.setContent(sibling, rect(100, 100, 50, 50, {0.8f, 0, 0, 1}));
    compositor.setZIndex(overlay, 10);
    assert(scheduler.tick(4.0));
    assert(rectIndex(sink.frames.back(), 0.9f) > rectIndex(sink.frames.back(), 0.8f));
    compositor.setZIndex(overlay, 0);
    assert(scheduler.tick(4.1));
    assert(rectIndex(sink.frames.back(), 0.9f) < rectIndex(sink.frames.back(), 0.8f));

    // Resize presents once with the new size; layer fragments survive; same size is a no-op.
    compositor.resize(1024, 768, 2);
    assert(scheduler.tick(5.0));
    assert(sink.frames.back().width == 1024 && sink.frames.back().dpi == 2 &&
           sink.frames.back().stats.layerHits == 2);
    compositor.resize(1024, 768, 2);
    assert(!scheduler.tick(5.1));

    // Animation: every animated tick presents; after completion the scene goes idle.
    const auto presented = sink.frames.size();
    scheduler.startAnimation(
        [&](double now)
        {
            const double t = std::min(1.0, (now - 6.0) / 0.1);
            compositor.setOpacity(panelA, static_cast<float>(1.0 - 0.5 * t));
            return t < 1.0;
        });
    double now = 6.016;
    while (scheduler.activeAnimations() > 0)
    {
        assert(scheduler.tick(now));
        assert(sink.frames.back().stats.layerMisses == 0); // opacity animation never re-lowers the panel
        now += 0.016;
    }
    const auto animated = sink.frames.size() - presented;
    assert(animated >= 6 && animated <= 8);
    for (int i = 0; i < 30; ++i)
        assert(!scheduler.tick(now + i * 0.016));
    assert(sink.frames.size() == presented + animated);

    // A paused animation (no property change) keeps ticking without submitting frames.
    const auto paused = scheduler.startAnimation([](double) { return true; });
    for (int i = 0; i < 10; ++i)
        assert(!scheduler.tick(now + 1 + i * 0.016));
    assert(scheduler.hasPendingWork() && scheduler.stopAnimation(paused) && !scheduler.hasPendingWork());

    // Capture re-presents the last snapshot without re-lowering.
    const auto beforeCapture = sink.frames.back().frame;
    scheduler.requestCapture("frame.bmp");
    assert(scheduler.tick(now + 2));
    assert(sink.captures == std::vector<std::string>{"frame.bmp"} &&
           sink.frames.back().frame == beforeCapture);
    assert(!scheduler.tick(now + 2.1));

    // Removing a cached layer drops its cache entry.
    assert(compositor.remove(panelB) && compositor.layerCache().entries == 1);
    assert(scheduler.tick(now + 3) && sink.frames.back().stats.layerHits == 1);
    assert(!compositor.remove(compositor.root()));

    // Effects: a drop shadow emits a tinted, blurred, offset copy of the body beneath it;
    // blur wraps the visual in a blurred group. Shadow and body share one retained LayerRef.
    {
        const auto card = compositor.createVisual(compositor.root());
        compositor.setLayer(card, true);
        compositor.setOffset(card, {50, 50});
        compositor.setContent(card, rect(0, 0, 100, 40, {0.6f, 0, 0, 1}));
        compositor.setEffects(card, {{Effect::Kind::kDropShadow, 4, {6, 8}, {0, 0, 0, 0.5f}}});
        const auto blurred = compositor.createVisual(compositor.root());
        compositor.setContent(blurred, rect(0, 0, 10, 10, {0.7f, 0, 0, 1}));
        compositor.setEffects(blurred, {{Effect::Kind::kBlur, 2}});
        assert(scheduler.tick(now + 3.5));
        const auto& frame = sink.frames.back();
        assert(frame.stats.effectsApplied == 2 && balanced(frame));
        std::vector<const SaveLayerCommand*> layers;
        std::vector<const LayerRef*> cardRefs;
        for (const auto& c : frame.displayList->commands())
        {
            if (const auto* l = std::get_if<SaveLayerCommand>(&c))
                layers.push_back(l);
            if (const auto* r = std::get_if<LayerRef>(&c); r && r->id == card)
                cardRefs.push_back(r);
        }
        const auto shadow = std::ranges::find_if(layers, [](auto* l) { return l->tint.has_value(); });
        assert(shadow != layers.end() && (*shadow)->blurSigma == 4 && (*shadow)->opacity == 0.5f &&
               (*shadow)->tint->a == 1);
        assert((*shadow)->bounds.x == -12 && (*shadow)->bounds.width == 124); // 3 sigma margin
        assert(cardRefs.size() == 2 && cardRefs[0]->commands == cardRefs[1]->commands);
        assert(std::ranges::any_of(layers, [](auto* l)
                                   { return l->blurSigma == 2 && !l->tint && l->bounds.x == -6; }));
        compositor.setEffects(card, {});
        compositor.remove(blurred);
        assert(scheduler.tick(now + 3.6) && sink.frames.back().stats.effectsApplied == 0);
        compositor.remove(card);
    }

    // Reorder changes paint order without touching z-index.
    {
        const auto a = compositor.createVisual(compositor.root());
        const auto b = compositor.createVisual(compositor.root());
        compositor.setContent(a, rect(0, 0, 1, 1, {0.41f, 0, 0, 1}));
        compositor.setContent(b, rect(0, 0, 1, 1, {0.42f, 0, 0, 1}));
        assert(scheduler.tick(now + 3.7));
        assert(rectIndex(sink.frames.back(), 0.41f) < rectIndex(sink.frames.back(), 0.42f));
        assert(compositor.reorder(b, 0) && compositor.children(compositor.root()).front() == b);
        assert(scheduler.tick(now + 3.8));
        assert(rectIndex(sink.frames.back(), 0.42f) < rectIndex(sink.frames.back(), 0.41f));
        compositor.remove(a);
        compositor.remove(b);
    }

    // Bounded cache: 40 layers of ~4 KiB each against a 64 KiB budget.
    for (int i = 0; i < 40; ++i)
    {
        const auto layer = compositor.createVisual(compositor.root());
        compositor.setLayer(layer, true);
        for (int j = 0; j < 20; ++j)
            compositor.setContent(compositor.createVisual(layer), rect(0, 0, 1, 1, {0, 0, 0, 1}));
    }
    assert(scheduler.tick(now + 4));
    const auto cache = compositor.layerCache();
    assert(cache.bytes <= cache.budget && cache.evictions > 0 && cache.entries < 41);

    const auto& t = scheduler.telemetry();
    std::printf(
        "composition: frames=%llu idle_ticks=%llu animation_frames=%llu captures=%llu cache_entries=%zu "
        "cache_bytes=%zu/%zu evictions=%llu\n",
        static_cast<unsigned long long>(t.presentedFrames), static_cast<unsigned long long>(t.idleTicks),
        static_cast<unsigned long long>(t.animationFrames), static_cast<unsigned long long>(t.captures),
        cache.entries, cache.bytes, cache.budget, static_cast<unsigned long long>(cache.evictions));
    return 0;
}
