#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <velora/runtime/property/PropertyTypes.hpp>

// Layered property storage of one object (R03.1/R03.3).
//
// Each (property, source) pair holds at most one value. The effective value is the value of the
// highest-precedence layer present; inheritance and metadata defaults are resolved one level up, by
// DependencyObject, because they need the object's parent and the property metadata.
//
// A layer may carry an expression — a {Binding}, {x:Bind}, {ThemeResource} or similar that computes
// the value. The expression lives *in* the layer it was applied to; it is not a precedence level of
// its own (V2 §1.A.4). The stored value is the expression's current result.

namespace velora
{

// Something that produces a layer's value over time. The store owns it with the layer.
class PropertyExpression
{
public:
    virtual ~PropertyExpression() = default;
    // Asked when a local value is written over this expression. UWP removes a OneWay {Binding} on
    // a local SetValue but keeps a TwoWay one (which forwards the value to its source). Returning
    // true keeps the expression in place.
    [[nodiscard]] virtual bool keepWhenOverwritten(const PropertyValue& newValue)
    {
        (void)newValue;
        return false;
    }
    // Called once when the expression leaves its layer (overwritten, cleared or the object went
    // away), so it can drop its subscriptions.
    virtual void detach() noexcept {}
};

class PropertyStore
{
public:
    PropertyStore() = default;
    ~PropertyStore();
    PropertyStore(const PropertyStore&) = delete;
    PropertyStore& operator=(const PropertyStore&) = delete;

    // The highest layer's value, or null when no layer holds the property. Inline: it is the
    // hottest call of layout and painting. An object holds a handful of entries, so a forward scan
    // beats a binary search; entries are sorted, so the last entry of `property` is its top layer.
    [[nodiscard]] const PropertyValue* effective(u64 property) const noexcept
    {
        const PropertyValue* result = nullptr;
        for (const auto& entry : m_entries)
        {
            if (entry.property == property)
                result = &entry.value;
            else if (entry.property > property)
                break;
        }
        return result;
    }
    // The highest layer strictly below `ceiling`, or null. Animation base values use this.
    [[nodiscard]] const PropertyValue* effectiveBelow(u64 property, PropertySource ceiling) const noexcept;
    [[nodiscard]] const PropertyValue* layer(u64 property, PropertySource source) const noexcept;
    [[nodiscard]] std::optional<PropertySource> source(u64 property) const noexcept;
    [[nodiscard]] PropertyExpression* expression(u64 property, PropertySource source) const noexcept;

    // Replaces the layer's value and expression. The previous expression is detached.
    void set(u64 property, PropertySource source, PropertyValue value,
             std::shared_ptr<PropertyExpression> expression = {});
    // Replaces only the value, keeping the layer's expression (an expression publishing a result).
    // False when the layer does not exist.
    bool update(u64 property, PropertySource source, PropertyValue value);
    // Removes the layer; false when there was none.
    bool clear(u64 property, PropertySource source);

    [[nodiscard]] usize size() const noexcept
    {
        return m_entries.size();
    }
    [[nodiscard]] usize approximateBytes() const noexcept
    {
        return m_entries.capacity() * sizeof(Entry);
    }

private:
    struct Entry
    {
        u64 property;
        PropertySource source;
        PropertyValue value;
        std::shared_ptr<PropertyExpression> expression;
    };
    // Sorted by (property, source): the last entry of a property is its effective one.
    std::vector<Entry> m_entries;

    std::vector<Entry>::iterator find(u64 property, PropertySource source) noexcept;
    std::vector<Entry>::const_iterator find(u64 property, PropertySource source) const noexcept;
};

} // namespace velora
