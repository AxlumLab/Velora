#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <velora/data/Observable.hpp>
#include <velora/reflect/Accessors.hpp>
#include <velora/runtime/ElementTree.hpp>
#include <velora/runtime/FrameworkSchema.hpp>

// {Binding Path=...}: dotted property paths evaluated against an element's DataContext at
// run time, through the accessor tables `velorac reflect` registers. Each binding caches the
// accessor found for every step (looked up again only when the object type at that step
// changes) and subscribes to every Observable along the path. Re-evaluation drops the old
// subscriptions first, so switching a DataContext leaves nothing attached to the old object.
// Bindings belong to their element and are destroyed with it.
namespace velora
{

enum class BindingMode
{
    kOneTime,
    kOneWay,
    kTwoWay
};

// IValueConverter (R09.3): converts a binding's source value for its target, and a TwoWay target's
// value back for its source. nullopt means "no value" (the binding then shows its fallback).
class IValueConverter
{
public:
    virtual ~IValueConverter() = default;
    [[nodiscard]] virtual std::optional<PropertyValue> convert(const PropertyValue& value,
                                                               std::string_view parameter) const = 0;
    [[nodiscard]] virtual std::optional<PropertyValue> convertBack(const PropertyValue& value,
                                                                   std::string_view parameter) const
    {
        (void)value;
        (void)parameter;
        return std::nullopt;
    }
};

// Converters a XAML resource can name by class ("<local:Negate x:Key=.../>" in namespace "using:App"
// is "App.Negate"). Applications register theirs before loading pages that use them.
void registerValueConverter(std::string className,
                            std::function<std::shared_ptr<const IValueConverter>()> factory);
[[nodiscard]] std::shared_ptr<const IValueConverter> createValueConverter(std::string_view className);

struct BindingOptions
{
    std::shared_ptr<const IValueConverter> converter;
    std::string converterParameter;
    // FallbackValue: shown while the binding cannot produce a value (no source, a broken path, a
    // converter that declines).
    std::optional<PropertyValue> fallback;
};

struct BindingStats
{
    std::size_t bindings = 0;
    std::size_t subscriptions = 0; // live subscriptions on source objects
    std::size_t evaluations = 0;
    std::size_t accessorLookups = 0; // accessor table searches (cache misses)
};

class BindingEngine
{
public:
    BindingEngine(ElementTree& tree, const PropertyDescriptor& dataContext);
    ~BindingEngine();
    BindingEngine(const BindingEngine&) = delete;
    BindingEngine& operator=(const BindingEngine&) = delete;

    // The path is relative to the element's DataContext; a binding of DataContext itself reads
    // the parent's. An empty path binds the DataContext object.
    void bind(ElementId element, const PropertyDescriptor& target, ValueKind kind, std::string_view path,
              BindingMode mode, BindingOptions options = {});
    // Re-evaluates the bindings in the subtree after `root`'s DataContext changed.
    void dataContextChanged(ElementId root);
    // After a user edit: writes TwoWay bindings of `target` on `element` back to their source.
    void writeBack(ElementId element, const PropertyDescriptor& target);

    BindingStats stats() const;
    const std::vector<std::string>& errors() const noexcept
    {
        return m_errors;
    }

private:
    struct Binding : std::enable_shared_from_this<Binding>
    {
        ElementId element = 0;
        const PropertyDescriptor* target = nullptr;
        ValueKind kind = ValueKind::kString;
        BindingMode mode = BindingMode::kOneWay;
        std::string path;
        std::vector<std::string> names;
        std::vector<PropertyId> ids;
        struct Step
        {
            const std::type_info* type = nullptr;
            const TypeAccessors* table = nullptr;
            const PropertyAccessor* accessor = nullptr;
        };
        std::vector<Step> cache;
        std::vector<Subscription> subscriptions;
        ObjectRef leafOwner; // object owning the last step, for write-back
        bool writing = false;
        bool evaluating = false;
        BindingOptions options;
    };

    void evaluate(Binding& b);
    const Binding::Step& step(Binding& b, std::size_t index, const ObjectRef& object);
    void fail(Binding& b, const std::string& message);
    void clearTarget(Binding& b);

    ElementTree& m_tree;
    const PropertyDescriptor& m_dataContext;
    std::unordered_map<ElementId, std::vector<std::shared_ptr<Binding>>> m_bindings;
    std::vector<std::string> m_errors;
    std::size_t m_evaluations = 0, m_lookups = 0;
};

} // namespace velora
