#pragma once

#include <concepts>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <velora/controls/XamlItems.hpp>
#include <velora/data/ObjectRef.hpp>
#include <velora/data/Observable.hpp>
#include <velora/runtime/property/PropertyTypes.hpp>

// {x:Bind} in the .vxbf pipeline (R09.4).
//
// x:Bind is compiled, not interpreted: velorac resolves the path against the x:Class's reflection
// metadata at build time and the generated code-behind provides one XBindAccessor per binding — a
// getter, the change subscriptions along the path (OneWay/TwoWay) and a setter (TwoWay). The .vxbf
// only records which accessor drives which property. No path is looked up by name at run time.
//
// toPropertyValue/fromPropertyValue are the conversions the generated accessors use; C++ overload
// resolution picks them from the member's declared type, so a type with no conversion is a compile
// error in the generated code, not a silent run-time failure.

namespace velora
{

struct XBindAccessor
{
    std::function<std::optional<PropertyValue>()> get;
    // Subscribes `changed` to every object along the path. Called again after every change: the
    // objects along the path may have been replaced. Empty for OneTime.
    std::function<std::vector<Subscription>(std::function<void()> changed)> observe;
    // Writes the target's value to the path's last member (TwoWay); false when the value does not
    // convert to the member's type.
    std::function<bool(const PropertyValue&)> set;
};

// The generated code-behind's accessors by x:Bind ordinal.
using XBindConnector = std::function<std::optional<XBindAccessor>(u32 index)>;

inline PropertyValue toPropertyValue(std::string_view value)
{
    return PropertyValue(std::string(value));
}
inline PropertyValue toPropertyValue(const std::string& value)
{
    return PropertyValue(value);
}
inline PropertyValue toPropertyValue(const char* value)
{
    return PropertyValue(std::string(value ? value : ""));
}
inline PropertyValue toPropertyValue(bool value)
{
    return PropertyValue(value);
}
template <std::integral T>
    requires(!std::same_as<T, bool>)
PropertyValue toPropertyValue(T value)
{
    return PropertyValue(static_cast<i64>(value));
}
template <std::floating_point T> PropertyValue toPropertyValue(T value)
{
    return PropertyValue(static_cast<f64>(value));
}
inline PropertyValue toPropertyValue(const PropertyValue& value)
{
    return value;
}
// An object (a DataContext from x:Bind): borrowed, the source must outlive the view.
template <class T>
    requires std::is_class_v<T> && (!std::same_as<std::remove_cv_t<T>, std::string>) &&
             (!std::same_as<std::remove_cv_t<T>, PropertyValue>)
PropertyValue toPropertyValue(T& value)
{
    return PropertyValue(ObjectRef::borrow(value));
}

// A collection for ItemsSource: adapted to IItemsSource (XamlItems.hpp) while T is known.
template <class T> PropertyValue toPropertyValue(ObservableVector<T>& value)
{
    return itemsSourceValue(value);
}

template <class T> std::optional<T> fromPropertyValue(const PropertyValue& value)
{
    if constexpr (std::same_as<T, std::string>)
        return value.isString() ? std::optional<T>(value.asString()) : std::nullopt;
    else if constexpr (std::same_as<T, bool>)
        return value.isBool() ? std::optional<T>(value.asBool()) : std::nullopt;
    else if constexpr (std::integral<T>)
    {
        if (value.isInt())
            return static_cast<T>(value.asInt());
        if (value.isDouble())
            return static_cast<T>(value.asDouble());
        return std::nullopt;
    }
    else if constexpr (std::floating_point<T>)
    {
        if (value.isDouble())
            return static_cast<T>(value.asDouble());
        if (value.isInt())
            return static_cast<T>(value.asInt());
        return std::nullopt;
    }
    else
    {
        static_assert(sizeof(T) == 0,
                      "x:Bind TwoWay: no conversion from a property value to this member type");
    }
}

} // namespace velora
