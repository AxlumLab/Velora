// R03 property system: precedence, expressions, animation base values, attached and inherited
// properties, read-only keys, validation, change notification safety and the ElementTree bridge.
// Each block maps to an item of the R03 acceptance list.
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <velora/composition/Compositor.hpp>
#include <velora/runtime/ElementTree.hpp>
#include <velora/runtime/Framework.hpp>
#include <velora/runtime/property/CoreProperties.hpp>
#include <velora/runtime/property/DependencyObject.hpp>

using namespace velora;

namespace
{
int g_failures = 0;

void check(bool condition, const char* what)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

double number(const DependencyObject& object, const PropertyDescriptor& property)
{
    return object.getValue(property).asDouble();
}

// A {Binding}-like expression: OneWay is removed by a local write, TwoWay keeps itself and
// forwards the written value to its source.
class TestBinding final : public PropertyExpression
{
public:
    explicit TestBinding(bool twoWay) : m_twoWay(twoWay) {}
    bool keepWhenOverwritten(const PropertyValue& value) override
    {
        if (m_twoWay)
            source = value;
        return m_twoWay;
    }
    void detach() noexcept override
    {
        ++detached;
    }
    PropertyValue source;
    int detached = 0;

private:
    bool m_twoWay;
};

void precedence()
{
    PropertyRegistry registry;
    const auto& opacity = registry.define("Opacity", PropertyValue(1.0), PropertyFlags::kPaint);
    DependencyObject object;

    check(number(object, opacity) == 1.0 && !object.valueSource(opacity), "unset: the metadata default");
    check(!object.readLocalValue(opacity), "readLocalValue is Unset without a local value");

    // Default < DefaultStyle < Style < Template < Local < Animation, independent of write order.
    object.setValue(opacity, PropertyValue(0.9), PropertySource::kLocal);
    object.setValue(opacity, PropertyValue(0.1), PropertySource::kDefaultStyle);
    object.setValue(opacity, PropertyValue(0.3), PropertySource::kTemplate);
    object.setValue(opacity, PropertyValue(0.2), PropertySource::kStyle);
    check(number(object, opacity) == 0.9 && object.valueSource(opacity) == PropertySource::kLocal,
          "Local wins over Template, Style and DefaultStyle");
    object.setValue(opacity, PropertyValue(0.5), PropertySource::kAnimation);
    check(number(object, opacity) == 0.5, "Animation wins over Local");

    // Animation base value (what the animation started from / returns to without HoldEnd).
    check(object.getAnimationBaseValue(opacity).asDouble() == 0.9, "the base value ignores the animation");
    check(object.readLocalValue(opacity)->asDouble() == 0.9, "readLocalValue sees only the local layer");

    // Clearing falls back one layer at a time.
    check(object.clearValue(opacity, PropertySource::kAnimation) && number(object, opacity) == 0.9,
          "clearing the animation (Stop) restores the local value");
    check(object.clearValue(opacity) && number(object, opacity) == 0.3,
          "clearing Local falls back to Template");
    check(object.clearValue(opacity, PropertySource::kTemplate) && number(object, opacity) == 0.2,
          "clearing Template falls back to Style");
    check(object.clearValue(opacity, PropertySource::kStyle) && number(object, opacity) == 0.1,
          "clearing a Style setter falls back to the type's default style");
    check(object.clearValue(opacity, PropertySource::kDefaultStyle) && number(object, opacity) == 1.0,
          "clearing everything returns to the metadata default");
    check(!object.clearValue(opacity), "clearing an absent layer reports false");
}

void expressions()
{
    PropertyRegistry registry;
    const auto& text = registry.define("Text", PropertyValue(std::string()), PropertyFlags::kMeasure);
    DependencyObject object;

    // A OneWay binding is a value in the Local layer, not an extra precedence level.
    auto oneWay = std::make_shared<TestBinding>(false);
    object.setExpression(text, PropertySource::kLocal, oneWay, PropertyValue("bound"));
    check(object.getValue(text).asString() == "bound" && object.valueSource(text) == PropertySource::kLocal,
          "a binding stores its result in its layer");
    object.setValue(text, PropertyValue("style"), PropertySource::kStyle);
    check(object.getValue(text).asString() == "bound", "a local binding still beats a Style setter");
    check(object.updateExpressionValue(text, PropertySource::kLocal, *oneWay, PropertyValue("updated")) &&
              object.getValue(text).asString() == "updated",
          "the binding publishes new source values");

    // A local SetValue replaces a OneWay binding (UWP), which detaches it.
    object.setValue(text, PropertyValue("typed"));
    check(object.getValue(text).asString() == "typed" && !object.expression(text, PropertySource::kLocal) &&
              oneWay->detached == 1,
          "a local value replaces a OneWay binding");
    check(!object.updateExpressionValue(text, PropertySource::kLocal, *oneWay, PropertyValue("stale")) &&
              object.getValue(text).asString() == "typed",
          "a replaced binding can no longer write");

    // A TwoWay binding stays and receives the value.
    auto twoWay = std::make_shared<TestBinding>(true);
    object.setExpression(text, PropertySource::kLocal, twoWay, PropertyValue("a"));
    object.setValue(text, PropertyValue("b"));
    check(object.expression(text, PropertySource::kLocal) == twoWay.get() &&
              twoWay->source.asString() == "b" && object.getValue(text).asString() == "b",
          "a TwoWay binding survives a local write and forwards it");
    check(object.clearValue(text) && twoWay->detached == 1 && object.getValue(text).asString() == "style",
          "clearValue removes the binding and falls back to the Style setter");
}

void metadata()
{
    PropertyRegistry registry;
    // Attached property: owned by Grid, set on any object.
    const auto& row = registry.define("Grid", "Row", PropertyValue(i64{0}), PropertyFlags::kArrange, false,
                                      PropertyKind::kAttached);
    DependencyObject child;
    child.setValue(row, PropertyValue(i64{2}));
    check(child.getValue(row).asInt() == 2 && row.kind == PropertyKind::kAttached && row.owner == "Grid",
          "an attached property is stored on the target object");

    // Inherited property (DataContext): resolved through the logical parent, overridden locally.
    const auto& context = registry.define("DataContext", PropertyValue(), PropertyFlags::kNone, true);
    DependencyObject parent, item;
    item.setInheritanceParent(&parent);
    parent.setValue(context, PropertyValue("model"));
    check(item.getValue(context).asString() == "model" && !item.valueSource(context),
          "an inherited value comes from the parent");
    item.setValue(context, PropertyValue("own"));
    check(item.getValue(context).asString() == "own", "a local value overrides the inherited one");
    item.setInheritanceParent(nullptr);
    item.clearValue(context);
    check(item.getValue(context).empty(), "without a parent the default applies");

    // Validation.
    PropertyDescriptor ranged;
    ranged.name = "Value";
    ranged.defaultValue = PropertyValue(0.0);
    ranged.validate = [](const PropertyValue& v) { return v.isDouble() && v.asDouble() >= 0; };
    const auto& value = registry.define(std::move(ranged));
    DependencyObject slider;
    check(slider.setValue(value, PropertyValue(-1.0)) == PropertySetResult::kInvalidValue &&
              slider.getValue(value).asDouble() == 0.0,
          "an invalid value is rejected and changes nothing");
    check(slider.setValue(value, PropertyValue(5.0)) == PropertySetResult::kOk, "a valid value is stored");

    // Read-only: only the registration key writes it.
    PropertyDescriptor pressed;
    pressed.name = "IsPressed";
    pressed.defaultValue = PropertyValue(false);
    const ReadOnlyPropertyKey key = registry.defineReadOnly(std::move(pressed));
    DependencyObject button;
    check(button.setValue(key.property(), PropertyValue(true)) == PropertySetResult::kReadOnly &&
              !button.getValue(key.property()).asBool(),
          "a read-only property rejects setValue");
    check(button.setValue(key, PropertyValue(true)) == PropertySetResult::kOk &&
              button.getValue(key.property()).asBool(),
          "the key writes a read-only property");
    check(!button.clearValue(key.property()) && button.clearValue(key), "only the key clears it");

    // Stable identity: derived from (owner, name), equal across registries, distinct per owner.
    PropertyRegistry other;
    const auto& otherRow = other.define("Grid", "Row", PropertyValue(i64{0}), PropertyFlags::kArrange, false,
                                        PropertyKind::kAttached);
    check(otherRow.id == row.id && row.id == propertyIdFor("Grid", "Row"), "property ids are stable");
    check(propertyIdFor("Grid", "Row") != propertyIdFor("Canvas", "Row") &&
              propertyIdFor("a.b", "c") != propertyIdFor("a", "b.c"),
          "property ids separate owners");
    bool duplicate = false;
    try
    {
        other.define("Grid", "Row", PropertyValue(), PropertyFlags::kNone, false, PropertyKind::kAttached);
    }
    catch (const std::runtime_error&)
    {
        duplicate = true;
    }
    check(duplicate, "a duplicate registration throws");
}

void notifications()
{
    PropertyRegistry registry;
    int metadataCalls = 0;
    PropertyDescriptor described;
    described.name = "Width";
    described.defaultValue = PropertyValue(0.0);
    described.changed = [&](DependencyObject&, const PropertyDescriptor&, const PropertyValue&,
                            const PropertyValue&) { ++metadataCalls; };
    const auto& width = registry.define(std::move(described));
    DependencyObject object;

    std::vector<std::pair<double, double>> seen;
    const u64 token = object.addPropertyChanged([&](DependencyObject&, const PropertyDescriptor&,
                                                    const PropertyValue& before, const PropertyValue& after)
                                                { seen.emplace_back(before.asDouble(), after.asDouble()); });
    check(token != 0, "handlers get a token");

    object.setValue(width, PropertyValue(10.0));
    object.setValue(width, PropertyValue(10.0)); // same effective value: no notification
    object.setValue(width, PropertyValue(5.0), PropertySource::kStyle); // hidden below Local: none
    check(seen.size() == 1 && seen[0] == std::pair(0.0, 10.0) && metadataCalls == 1,
          "notifications only for effective changes, metadata callback first");

    // Batching coalesces and drops round trips.
    seen.clear();
    {
        DependencyObject::ChangeBatch batch(object);
        object.setValue(width, PropertyValue(20.0));
        object.setValue(width, PropertyValue(30.0));
        check(seen.empty(), "a batch defers notifications");
    }
    check(seen.size() == 1 && seen[0] == std::pair(10.0, 30.0),
          "a batch coalesces to first-old and final value");
    seen.clear();
    {
        DependencyObject::ChangeBatch batch(object);
        object.setValue(width, PropertyValue(1.0));
        object.setValue(width, PropertyValue(30.0));
    }
    check(seen.empty(), "a change that ends where it started notifies nothing");

    // Token protocol.
    check(object.removePropertyChanged(token) && !object.removePropertyChanged(token),
          "removing a token works once");
    object.setValue(width, PropertyValue(40.0));
    check(seen.empty(), "a removed handler is not called");

    // A handler removing another during dispatch: the other is skipped.
    int second = 0;
    u64 secondToken = 0;
    object.addPropertyChanged([&](auto&, auto&, auto&, auto&) { object.removePropertyChanged(secondToken); });
    secondToken = object.addPropertyChanged([&](auto&, auto&, auto&, auto&) { ++second; });
    object.setValue(width, PropertyValue(50.0));
    check(second == 0, "a handler removed during dispatch is skipped");

    // Re-entrancy: a handler that keeps changing what it observes is bounded.
    DependencyObject loop;
    loop.addPropertyChanged(
        [&](DependencyObject& o, const PropertyDescriptor& p, const PropertyValue&, const PropertyValue& now)
        { o.setValue(p, PropertyValue(now.asDouble() + 1)); });
    loop.setValue(width, PropertyValue(1.0));
    check(loop.droppedNotifications() > 0 && number(loop, width) > 1, "runaway re-entrancy is bounded");

    // A handler destroying the object it observes (no use-after-free; ASan checks this).
    auto doomed = std::make_unique<DependencyObject>();
    int after = 0;
    doomed->addPropertyChanged([&](auto&, auto&, auto&, auto&) { doomed.reset(); });
    doomed->addPropertyChanged([&](auto&, auto&, auto&, auto&) { ++after; });
    doomed->setValue(width, PropertyValue(1.0));
    check(!doomed && after == 0, "dispatch stops when a handler destroys the object");

    // Thread affinity.
    DependencyObject owned;
    PropertySetResult fromOther = PropertySetResult::kOk;
    std::thread([&] { fromOther = owned.setValue(width, PropertyValue(1.0)); }).join();
    check(fromOther == PropertySetResult::kWrongThread && number(owned, width) == 0.0,
          "writes off the owner thread are refused");
}

void treeBridge()
{
    PropertyRegistry registry;
    const auto& opacity = registry.define("Opacity", PropertyValue(1.0), PropertyFlags::kPaint);
    const auto& context = registry.define("DataContext", PropertyValue(), PropertyFlags::kNone, true);

    // The tree reads and writes through the element's DependencyObject: one value source.
    ElementTree tree;
    const ElementId root = tree.create("Grid");
    const ElementId leaf = tree.create("TextBlock", root);
    tree.setProperty(leaf, opacity, PropertyValue(0.5), PropertySource::kLocal);
    const auto object = tree.object(leaf);
    check(object && number(*object, opacity) == 0.5 && tree.element(*object) == leaf,
          "an element's values live in its DependencyObject");
    object->setValue(opacity, PropertyValue(0.25));
    check(tree.property(leaf, opacity).asDouble() == 0.25 && tree.paintDirty(leaf),
          "a write through the object is the tree's value and invalidates the element");

    tree.setProperty(root, context, PropertyValue("model"), PropertySource::kLocal);
    check(object->getValue(context).asString() == "model", "the tree links inheritance parents");

    // The PropertyStore is not owned by the tree: the object keeps its values after removal.
    tree.remove(leaf);
    check(tree.element(*object) == 0 && number(*object, opacity) == 0.25 && !object->inheritanceParent(),
          "a removed element's object keeps its values and is unlinked");
    check(object->setValue(opacity, PropertyValue(0.75)) == PropertySetResult::kOk,
          "a detached object stays writable");

    // Framework and the core properties name the same properties.
    Compositor compositor;
    Framework ui(compositor);
    const auto& core = coreProperties();
    const auto same = [](const PropertyDescriptor* a, const PropertyDescriptor* b)
    { return a && b && a->id == b->id && a->defaultValue == b->defaultValue && a->flags == b->flags; };
    check(same(core.opacity, ui.props().Opacity), "core Opacity matches Framework's (id, default, flags)");
    check(same(core.isVisible, ui.props().Visibility), "core IsVisible matches Framework's Visibility");
    check(same(core.width, ui.props().Width), "core Width matches Framework's");
    check(same(core.isEnabled, ui.props().IsEnabled), "core IsEnabled matches Framework's");
    check(same(core.content, ui.props().Content), "core Content matches Framework's");

    // Type defaults sit below application styles: clearing a Style setter restores them.
    const ElementId button = ui.create(ElementKind::kButton);
    const auto padding = ui.tree().property(button, *ui.props().Padding);
    ui.set(button, ui.props().Padding, PropertyValue(Thickness{1, 1, 1, 1}), PropertySource::kStyle);
    ui.clear(button, ui.props().Padding, PropertySource::kStyle);
    check(ui.tree().property(button, *ui.props().Padding) == padding &&
              ui.tree().propertySource(button, *ui.props().Padding) == PropertySource::kDefaultStyle,
          "clearing a Style setter falls back to the type default");
}
} // namespace

int main()
{
    precedence();
    expressions();
    metadata();
    notifications();
    treeBridge();
    std::printf("%s (%d failure%s)\n", g_failures ? "FAIL" : "PASS", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
