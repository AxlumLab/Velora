#include <cassert>
#include <cstdio>
#include <velora/runtime/ElementTree.hpp>

using namespace velora;

static void clearAll(ElementTree& tree, ElementId id)
{
    tree.clearDirty(id);
    for (const auto child : tree.children(id))
        clearAll(tree, child);
}

int main()
{
    PropertyRegistry properties;
    const auto& opacity = properties.define("Opacity", PropertyValue(1.0), PropertyFlags::kPaint, true);
    const auto& width = properties.define("Width", PropertyValue(0.0), PropertyFlags::kMeasure);
    const auto& text =
        properties.define("Text", PropertyValue(""), PropertyFlags::kMeasure | PropertyFlags::kPaint);
    assert(properties.find("Opacity") == &opacity && properties.find("Opacity")->inherited);
    assert(properties.find("Missing") == nullptr);
    bool duplicateRejected = false;
    try
    {
        properties.define("Width", PropertyValue(1.0), PropertyFlags::kNone);
    }
    catch (const std::runtime_error&)
    {
        duplicateRejected = true;
    }
    assert(duplicateRejected);

    // Grid > StackPanel > {TextBlock, Button}: a render-less retained tree.
    ElementTree tree;
    const auto grid = tree.create("Grid");
    const auto stack = tree.create("StackPanel", grid);
    const auto label = tree.create("TextBlock", stack);
    const auto button = tree.create("Button", stack);
    assert(tree.create("Orphan", 999) == 0);
    assert(tree.parent(label) == stack && tree.children(stack).size() == 2 && tree.type(button) == "Button");
    assert(tree.measureDirty(label) && tree.measureDirty(grid)); // new elements need a first layout pass

    // Precedence, fallback on removal, inheritance.
    clearAll(tree, grid);
    assert(tree.property(label, opacity).asDouble() == 1.0);
    assert(tree.setProperty(grid, opacity, PropertyValue(0.5), PropertySource::kStyle));
    assert(tree.property(label, opacity).asDouble() == 0.5);
    assert(tree.paintDirty(label) && !tree.measureDirty(label));
    assert(tree.setProperty(label, opacity, PropertyValue(0.8), PropertySource::kLocal));
    assert(tree.setProperty(label, opacity, PropertyValue(0.3), PropertySource::kDefaultStyle));
    assert(tree.property(label, opacity).asDouble() == 0.8 &&
           tree.propertySource(label, opacity) == PropertySource::kLocal);
    assert(tree.setProperty(label, opacity, PropertyValue(0.9), PropertySource::kAnimation));
    assert(tree.property(label, opacity).asDouble() == 0.9);
    assert(tree.clearProperty(label, opacity, PropertySource::kAnimation));
    assert(tree.property(label, opacity).asDouble() == 0.8);
    assert(tree.clearProperty(label, opacity, PropertySource::kLocal));
    assert(tree.property(label, opacity).asDouble() == 0.3);
    assert(tree.clearProperty(label, opacity, PropertySource::kDefaultStyle));
    assert(tree.property(label, opacity).asDouble() == 0.5 && !tree.propertySource(label, opacity));
    assert(!tree.clearProperty(label, opacity, PropertySource::kDefaultStyle));
    assert(tree.property(stack, width).asDouble() == 0.0);
    tree.setProperty(stack, width, PropertyValue(40.0), PropertySource::kLocal);
    assert(tree.property(label, width).asDouble() == 0.0); // non-inherited

    // Dirty propagation: measure bubbles to ancestors, never to siblings.
    clearAll(tree, grid);
    assert(tree.setProperty(label, width, PropertyValue(42.0), PropertySource::kLocal));
    assert(tree.measureDirty(label) && tree.measureDirty(stack) && tree.measureDirty(grid));
    assert(!tree.measureDirty(button) && !tree.paintDirty(label));
    clearAll(tree, grid);
    assert(tree.reorder(button, 0) && tree.children(stack).front() == button);
    assert(tree.arrangeDirty(stack) && !tree.measureDirty(stack));

    // Notifications fire only when the effective value changes.
    int changes = 0;
    int secondObserver = 0;
    const auto subscription = tree.subscribe([&](ElementId, const PropertyDescriptor&) { ++changes; });
    const auto other = tree.subscribe([&](ElementId, const PropertyDescriptor&) { ++secondObserver; });
    tree.setProperty(button, text, PropertyValue("OK"), PropertySource::kLocal);
    tree.setProperty(button, text, PropertyValue("OK"), PropertySource::kLocal);
    tree.setProperty(button, text, PropertyValue("Theme"), PropertySource::kDefaultStyle);
    assert(changes == 1 && tree.property(button, text).asString() == "OK");
    tree.clearProperty(button, text, PropertySource::kLocal);
    assert(changes == 2 && tree.property(button, text).asString() == "Theme");
    tree.unsubscribe(other);
    tree.setProperty(button, text, PropertyValue("Other"), PropertySource::kLocal);
    assert(changes == 3 && secondObserver == 2);
    tree.unsubscribe(subscription);

    // Structure version and roots.
    const auto version = tree.structureVersion();
    const auto extraRoot = tree.create("Popup");
    assert(tree.roots().size() == 2 && tree.roots().back() == extraRoot &&
           tree.structureVersion() == version + 1);
    tree.reorder(label, 1);
    assert(tree.structureVersion() == version + 2);
    tree.remove(extraRoot);
    assert(tree.roots().size() == 1 && tree.structureVersion() == version + 3);

    // Routing, focus, capture.
    bool bubbled = false;
    tree.on(grid, 7,
            [&]
            {
                bubbled = true;
                return true;
            });
    assert(tree.dispatch(button, 7) && bubbled);
    assert(!tree.dispatch(button, 8));
    assert(tree.focus(button) && tree.focused() == button);
    assert(tree.capturePointer(label) && tree.pointerCapture() == label);

    // Removing an inner subtree detaches it, clears focus/capture and dirties the parent.
    clearAll(tree, grid);
    assert(tree.remove(stack));
    assert(tree.children(grid).empty() && !tree.contains(label) && !tree.contains(button));
    assert(tree.focused() == 0 && tree.pointerCapture() == 0 && tree.measureDirty(grid));
    assert(tree.node_count() == 1 && !tree.remove(stack));

    // Deep and wide subtree removal.
    ElementId cursor = grid;
    for (int i = 0; i < 64; ++i)
    {
        cursor = tree.create("Border", cursor);
        tree.create("TextBlock", cursor);
    }
    assert(tree.node_count() == 129);
    assert(tree.remove(tree.children(grid).front()) && tree.node_count() == 1);

    // 100000-row virtual list: only the realized window becomes elements.
    VirtualList list;
    list.setCount(100000);
    list.setRowHeight(20);
    list.setViewport(200);
    list.scrollTo(50000);
    assert(list.count() == 100000 && list.visibleCount() == 11);
    const auto host = tree.create("ItemsRepeater", grid);
    for (std::size_t i = 0; i < list.visibleCount(); ++i)
    {
        const auto row = tree.create("TextBlock", host);
        tree.setProperty(row, text, PropertyValue("row " + std::to_string(list.firstVisible() + i)),
                         PropertySource::kLocal);
    }
    const auto realized = tree.stats();
    assert(realized.nodes == 13 && realized.propertyValues == 12); // 11 row texts + grid opacity
    list.scrollTo(99999);
    assert(list.visibleCount() == 1);
    list.setCount(200000);
    assert(list.count() == VirtualList::kMaxItems);

    // Memory record for a fully realized 100000-element tree, for comparison with the virtualized one.
    ElementTree full;
    const auto fullRoot = full.create("StackPanel");
    for (std::size_t i = 0; i < VirtualList::kMaxItems; ++i)
        full.setProperty(full.create("TextBlock", fullRoot), width, PropertyValue(1.0),
                         PropertySource::kLocal);
    const auto all = full.stats();
    assert(all.nodes == VirtualList::kMaxItems + 1 && all.propertyValues == VirtualList::kMaxItems);
    std::printf("element-tree memory: virtualized nodes=%zu bytes=%zu; realized nodes=%zu bytes=%zu (%.1f "
                "bytes/node)\n",
                realized.nodes, realized.approximateBytes, all.nodes, all.approximateBytes,
                static_cast<double>(all.approximateBytes) / static_cast<double>(all.nodes));
    assert(full.remove(fullRoot) && full.node_count() == 0);
    return 0;
}
