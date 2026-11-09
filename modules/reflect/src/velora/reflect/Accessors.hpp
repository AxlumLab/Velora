#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <velora/data/ObjectRef.hpp>
#include <velora/data/Observable.hpp>
#include <velora/globals.hpp>
#include <velora/reflect/Reflect.hpp>

// Runtime access to reflected properties and public fields, for bindings whose source type
// is only known at run time ({Binding} on a DataContext). `velorac reflect` emits one
// TypeAccessors table per reflected class into .reflect.cpp; nothing here parses metadata.
// Scalars travel as ReflectValue; properties that yield a class object by reference are
// followed as ObjectRef.
namespace velora
{

struct PropertyAccessor
{
    std::string_view name;
    ReflectValue (*get)(const void* object) = nullptr;              // scalar or string value
    bool (*set)(void* object, const ReflectValue& value) = nullptr; // false when the value kind does not fit
    ObjectRef (*getObject)(void* object) = nullptr;                 // class object returned by reference
};

struct TypeAccessors
{
    const std::type_info* type = nullptr;
    std::string_view name;
    std::span<const PropertyAccessor> properties;
    Observable* (*observable)(void* object) = nullptr;

    const PropertyAccessor* find(std::string_view property) const noexcept
    {
        for (const auto& p : properties)
            if (p.name == property)
                return &p;
        return nullptr;
    }
};

// Called from generated code during static initialization; `accessors` must outlive the program.
bool registerAccessors(const TypeAccessors& accessors);
const TypeAccessors* findAccessors(const std::type_info& type);

template <class T> using BareType = std::remove_cvref_t<T>;
template <class T>
inline constexpr bool kIsReflectString =
    std::is_convertible_v<const T&, std::string_view> && !std::is_same_v<BareType<T>, std::nullptr_t>;
template <class T>
inline constexpr bool kIsReflectScalar =
    kIsReflectString<BareType<T>> || std::is_arithmetic_v<BareType<T>> || std::is_enum_v<BareType<T>>;

template <class T> ReflectValue toReflectValue(const T& v)
{
    using U = BareType<T>;
    if constexpr (kIsReflectString<U>)
        return ReflectValue(std::string(std::string_view(v)));
    else if constexpr (std::is_same_v<U, bool>)
        return ReflectValue(v);
    else if constexpr (std::is_floating_point_v<U>)
        return ReflectValue(static_cast<double>(v));
    else
        return ReflectValue(static_cast<i64>(v));
}

template <class T> bool fromReflectValue(const ReflectValue& v, T& out)
{
    if constexpr (std::is_same_v<T, std::string>)
    {
        if (v.kind() != ReflectValueKind::kString)
            return false;
        out = std::string(v.asString());
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        if (v.kind() != ReflectValueKind::kBool)
            return false;
        out = v.asBool();
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        if (v.kind() == ReflectValueKind::kF64)
            out = static_cast<T>(v.asF64());
        else if (v.kind() == ReflectValueKind::kI64)
            out = static_cast<T>(v.asI64());
        else
            return false;
    }
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
    {
        if (v.kind() != ReflectValueKind::kI64)
            return false;
        out = static_cast<T>(v.asI64());
    }
    else
    {
        return false;
    }
    return true;
}

// Builds an accessor from captureless lambdas: Get is `(void*) -> decltype(auto)` returning the
// property; Set (optional) is `(void*, auto&& value)`. Which entry points exist follows from
// the getter's return type, so the generator need not classify C++ types.
template <class Get, class Set = std::nullptr_t>
PropertyAccessor property(std::string_view name, Get, Set = nullptr)
{
    using R = decltype(Get{}(static_cast<void*>(nullptr)));
    PropertyAccessor a{name};
    if constexpr (kIsReflectScalar<R>)
    {
        a.get = [](const void* o) { return toReflectValue(Get{}(const_cast<void*>(o))); };
        if constexpr (!std::is_same_v<Set, std::nullptr_t>)
        {
            a.set = [](void* o, const ReflectValue& v)
            {
                using V = std::conditional_t<kIsReflectString<BareType<R>>, std::string, BareType<R>>;
                V value{};
                if (!fromReflectValue(v, value))
                    return false;
                Set{}(o, std::move(value));
                return true;
            };
        }
    }
    else if constexpr (std::is_lvalue_reference_v<R> && std::is_class_v<BareType<R>>)
    {
        a.getObject = [](void* o)
        {
            auto& r = Get{}(o);
            return ObjectRef{const_cast<void*>(static_cast<const void*>(std::addressof(r))),
                             &typeid(BareType<R>)};
        };
    }
    return a;
}

template <class T> Observable* observableOf(void* object)
{
    if constexpr (std::is_base_of_v<Observable, T>)
        return static_cast<T*>(object);
    else
        return nullptr;
}

} // namespace velora
