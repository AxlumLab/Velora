#include <velora/globals.hpp>
// ElementTree -> composition bridge: structure mirroring, render-property updates without
// repaint, content repaint, fallback on property removal, reorder, removal and scale.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <variant>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/graphics/Canvas.hpp>
#include <velora/runtime/VisualBridge.hpp>

using namespace velora;
using namespace velora;

namespace
{

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

std::vector<std::string> texts(const SceneSnapshot& scene)
{
    std::vector<Command> flat;
    expand(scene.displayList->commands(), flat);
    std::vector<std::string> out;
    for (const auto& c : flat)
        if (const auto* t = std::get_if<TextCommand>(&c))
            out.push_back(t->value);
    return out;
}

} // namespace

int main()
{
    PropertyRegistry registry;
    const auto props = VisualProperties::define(registry);
    const auto& text =
        registry.define("Text", PropertyValue(""), PropertyFlags::kMeasure | PropertyFlags::kPaint);
    const auto set = [](ElementTree& t, ElementId e, const PropertyDescriptor& d, PropertyValue v)
    { t.setProperty(e, d, std::move(v), PropertySource::kLocal); };

    ElementTree tree;
    Compositor compositor;
    VisualBridge bridge(tree, compositor, props);
    int paintedText = 0;
    bridge.setPainter("TextBlock",
                      [&](Canvas& canvas, const ElementTree& t, ElementId e, float w, float h)
                      {
                          ++paintedText;
                          canvas.drawText({0, 0, w, h}, t.property(e, text).asString(), 14, {{1, 1, 1, 1}});
                      });

    // Grid > StackPanel > {TextBlock, Button}
    const auto grid = tree.create("Grid");
    const auto panel = tree.create("StackPanel", grid);
    const auto label = tree.create("TextBlock", panel);
    const auto button = tree.create("Button", panel);
    set(tree, grid, *props.width, 800.0);
    set(tree, grid, *props.height, 600.0);
    set(tree, panel, *props.left, 10.0);
    set(tree, panel, *props.top, 20.0);
    set(tree, panel, *props.width, 300.0);
    set(tree, panel, *props.height, 200.0);
    set(tree, panel, *props.background, velora::i64{0xFF202020});
    set(tree, label, *props.width, 300.0);
    set(tree, label, *props.height, 20.0);
    set(tree, label, text, "Hello");
    set(tree, button, *props.top, 30.0);
    set(tree, button, *props.width, 120.0);
    set(tree, button, *props.height, 32.0);
    set(tree, button, *props.background, velora::i64{0xFF0078D4});
    bridge.sync();

    assert(bridge.stats().created == 4 && bridge.stats().repaints == 4 && paintedText == 1);
    const auto vGrid = bridge.visual(grid), vPanel = bridge.visual(panel), vLabel = bridge.visual(label),
               vButton = bridge.visual(button);
    assert(compositor.children(compositor.root()) == std::vector{vGrid});
    assert(compositor.children(vGrid) == std::vector{vPanel});
    assert((compositor.children(vPanel) == std::vector{vLabel, vButton}));
    auto scene = compositor.snapshot();
    assert(texts(scene) == std::vector<std::string>{"Hello"});
    {
        // Panel offset and background reach the display list.
        bool offset = false, background = false;
        for (const auto& c : scene.displayList->commands())
        {
            if (const auto* t = std::get_if<TransformCommand>(&c);
                t && t->transform.translateX == 10 && t->transform.translateY == 20)
                offset = true;
            if (const auto* r = std::get_if<RectCommand>(&c);
                r && r->bounds.width == 300 && r->paint.color.r > 0.12f && r->paint.color.r < 0.13f)
                background = true;
        }
        assert(offset && background);
    }

    // Render properties update the visual without repainting.
    const auto repaints = bridge.stats().repaints;
    set(tree, button, *props.opacity, 0.5);
    set(tree, button, *props.left, 40.0);
    bridge.sync();
    assert(bridge.stats().repaints == repaints && bridge.stats().propertyUpdates == 1);
    assert(compositor.needsFrame());
    scene = compositor.snapshot();
    assert(scene.stats.visualsLowered == 5);

    // Content properties repaint only their element.
    set(tree, label, text, "World");
    bridge.sync();
    assert(bridge.stats().repaints == repaints + 1 && paintedText == 2);
    assert(texts(compositor.snapshot()) == std::vector<std::string>{"World"});

    // Clearing a local value falls back and is mirrored.
    tree.clearProperty(button, *props.opacity, PropertySource::kLocal);
    tree.setProperty(button, *props.opacity, PropertyValue(0.8), PropertySource::kStyle);
    bridge.sync();
    assert(tree.property(button, *props.opacity).asDouble() == 0.8);

    // Visibility hides the subtree; no change means no frame.
    set(tree, label, *props.visible, false);
    bridge.sync();
    assert(texts(compositor.snapshot()).empty());
    bridge.sync();
    assert(!compositor.needsFrame());
    set(tree, label, *props.visible, true);

    // Reorder and z-index.
    tree.reorder(button, 0);
    bridge.sync();
    assert((compositor.children(vPanel) == std::vector{vButton, vLabel}) && bridge.stats().reordered >= 1);

    // Removal removes the visual subtree.
    const auto popup = tree.create("Popup");
    set(tree, popup, *props.zIndex, velora::i64{10});
    bridge.sync();
    assert((compositor.children(compositor.root()) == std::vector{vGrid, bridge.visual(popup)}));
    tree.remove(panel);
    bridge.sync();
    assert(!compositor.contains(vPanel) && !compositor.contains(vLabel) && !compositor.contains(vButton));
    assert(bridge.visual(label) == 0 && bridge.stats().removed == 3);
    tree.remove(popup);
    bridge.sync();
    assert(compositor.children(compositor.root()) == std::vector{vGrid});

    // Scale: 2000 elements created, then one property changed.
    using clock = std::chrono::steady_clock;
    const auto list = tree.create("ItemsControl", grid);
    for (int i = 0; i < 2000; ++i)
    {
        const auto row = tree.create("TextBlock", list);
        set(tree, row, *props.top, i * 20.0);
        set(tree, row, *props.width, 200.0);
        set(tree, row, *props.height, 20.0);
        set(tree, row, text, "row " + std::to_string(i));
    }
    auto begin = clock::now();
    bridge.sync();
    const double fullMs = std::chrono::duration<double, std::milli>(clock::now() - begin).count();
    const auto row = tree.children(list)[1000];
    const auto before = bridge.stats();
    set(tree, row, *props.opacity, 0.3);
    begin = clock::now();
    bridge.sync();
    const double oneMs = std::chrono::duration<double, std::milli>(clock::now() - begin).count();
    assert(bridge.stats().repaints == before.repaints &&
           bridge.stats().propertyUpdates == before.propertyUpdates + 1);
    std::printf("visual bridge: 2001 elements created in %.2f ms, single opacity change synced in %.3f ms\n",
                fullMs, oneMs);
    return 0;
}
