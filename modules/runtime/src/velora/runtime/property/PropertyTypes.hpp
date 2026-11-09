#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <velora/data/ObjectRef.hpp>
#include <velora/globals.hpp>

// Property value types and dependency property identities (R03.2).
//
// Split out of element_tree.hpp so the object-level property store (PropertyStore,
// DependencyObject) does not depend on the legacy UI tree. ElementTree includes this header and
// keeps working unchanged.

namespace velora
{

// Value sources ordered by precedence: a later enumerator overrides an earlier one (R03.3).
//
// This is UWP's dependency property precedence, lowest first, with the metadata default below
// every layer (it is not stored):
//   Default (metadata) < DefaultStyle < Style < Template < Local < Animation
// kDefaultStyle holds a type's built-in look (the schema defaults applied by Framework::create;
// later the Fluent default style), so clearing an application Style setter falls back to it rather
// than to the bare metadata default. {Binding}, {x:Bind}, {StaticResource} and {ThemeResource} are
// not layers: they are expressions stored in whichever layer they were applied to.
enum class PropertySource : u8
{
    kDefaultStyle,
    kStyle,
    kTemplate,
    kLocal,
    kAnimation
};

// Every stored layer, lowest precedence first.
inline constexpr PropertySource kPropertySources[] = {PropertySource::kDefaultStyle, PropertySource::kStyle,
                                                      PropertySource::kTemplate, PropertySource::kLocal,
                                                      PropertySource::kAnimation};

enum class PropertyFlags : u8
{
    kNone = 0,
    kMeasure = 1,
    kArrange = 2,
    kPaint = 4
};
inline PropertyFlags operator|(PropertyFlags a, PropertyFlags b)
{
    return static_cast<PropertyFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool hasFlag(PropertyFlags value, PropertyFlags flag)
{
    return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) != 0;
}

struct Thickness
{
    double left = 0, top = 0, right = 0, bottom = 0;
    friend bool operator==(const Thickness&, const Thickness&) = default;
};

// Grid row/column size: pixels, Auto, or a weighted share of the remaining space.
struct GridLength
{
    enum class Unit : u8
    {
        kPixel,
        kAuto,
        kStar
    } unit = Unit::kStar;
    double value = 1;
    friend bool operator==(const GridLength&, const GridLength&) = default;
};
using GridLengths = std::vector<GridLength>;

struct ElementContentRef
{
    u64 element = 0;
    std::weak_ptr<void> runtime;
    bool operator==(const ElementContentRef& other) const noexcept
    {
        return element == other.element && !runtime.owner_before(other.runtime) &&
               !other.runtime.owner_before(runtime);
    }
};

class PropertyValue
{
public:
    PropertyValue() = default;
    PropertyValue(bool v) : value_(v) {}
    PropertyValue(i64 v) : value_(v) {}
    PropertyValue(double v) : value_(v) {}
    PropertyValue(std::string v) : value_(std::move(v)) {}
    PropertyValue(const char* v) : value_(std::string(v)) {}
    PropertyValue(Thickness v) : value_(v) {}
    PropertyValue(GridLengths v) : value_(std::move(v)) {}
    PropertyValue(ElementContentRef v) : value_(std::move(v)) {}
    PropertyValue(ObjectRef v) : value_(std::move(v)) {}

    bool empty() const noexcept
    {
        return std::holds_alternative<std::monostate>(value_);
    }
    bool isBool() const noexcept
    {
        return std::holds_alternative<bool>(value_);
    }
    bool isInt() const noexcept
    {
        return std::holds_alternative<i64>(value_);
    }
    bool isDouble() const noexcept
    {
        return std::holds_alternative<double>(value_);
    }
    bool isString() const noexcept
    {
        return std::holds_alternative<std::string>(value_);
    }
    bool isThickness() const noexcept
    {
        return std::holds_alternative<Thickness>(value_);
    }
    bool isGridLengths() const noexcept
    {
        return std::holds_alternative<GridLengths>(value_);
    }
    bool isElement() const noexcept
    {
        return std::holds_alternative<ElementContentRef>(value_);
    }
    const ElementContentRef& asElement() const
    {
        return get<ElementContentRef>("element content");
    }
    bool isObject() const noexcept
    {
        return std::holds_alternative<ObjectRef>(value_);
    }
    bool asBool() const
    {
        return get<bool>("bool");
    }
    i64 asInt() const
    {
        return get<i64>("int");
    }
    double asDouble() const
    {
        return get<double>("double");
    }
    const std::string& asString() const
    {
        return get<std::string>("string");
    }
    const Thickness& asThickness() const
    {
        return get<Thickness>("thickness");
    }
    const GridLengths& asGridLengths() const
    {
        return get<GridLengths>("grid lengths");
    }
    const ObjectRef& asObject() const
    {
        return get<ObjectRef>("object");
    }

    friend bool operator==(const PropertyValue&, const PropertyValue&) = default;

private:
    template <class T> const T& get(const char* name) const
    {
        if (const auto* v = std::get_if<T>(&value_))
            return *v;
        throw std::runtime_error(std::string("property is not ") + name);
    }
    std::variant<std::monostate, bool, i64, double, std::string, Thickness, GridLengths, ObjectRef,
                 ElementContentRef>
        value_;
};

enum class PropertyKind : u8
{
    kNormal,
    kContent,  // the property that receives a XAML element's child content
    kAttached, // set on other elements, e.g. Grid.Row
};

class DependencyObject;

// A dependency property: identity plus metadata (R03.2). Registered once and referenced by address.
//
// `id` is derived from (owner, name) — propertyIdFor — so every registry, process and build agrees
// on it and it never depends on registration order; property stores are keyed by it.
struct PropertyDescriptor
{
    u64 id = 0;
    std::string name;
    PropertyValue defaultValue;
    PropertyFlags flags = PropertyFlags::kNone;
    bool inherited = false;
    std::string owner; // owning type; empty for framework-wide properties
    PropertyKind kind = PropertyKind::kNormal;
    // Read-only properties reject DependencyObject::setValue(property, ...); only the holder of the
    // ReadOnlyPropertyKey returned at registration can write them.
    bool readOnly = false;
    // Value validation; null accepts every value. An empty value (clearing) is never validated.
    std::function<bool(const PropertyValue&)> validate;
    // PropertyChangedCallback: called after the effective value changed, before instance handlers.
    std::function<void(DependencyObject&, const PropertyDescriptor&, const PropertyValue& oldValue,
                       const PropertyValue& newValue)>
        changed;
};

using DependencyProperty = PropertyDescriptor;

// The stable id of the property `name` owned by `owner` (empty owner: framework-wide).
[[nodiscard]] u64 propertyIdFor(std::string_view owner, std::string_view name) noexcept;

// Write access to a read-only property. Only PropertyRegistry::defineReadOnly creates one; the code
// that registered the property keeps it.
class ReadOnlyPropertyKey
{
public:
    [[nodiscard]] const PropertyDescriptor& property() const noexcept
    {
        return *m_property;
    }

private:
    friend class PropertyRegistry;
    explicit ReadOnlyPropertyKey(const PropertyDescriptor& property) noexcept : m_property(&property) {}
    const PropertyDescriptor* m_property;
};

// Descriptors are registered once and referenced by address; the registry keeps them stable.
class PropertyRegistry
{
public:
    const PropertyDescriptor& define(std::string name, PropertyValue defaultValue, PropertyFlags flags,
                                     bool inherited = false);
    // Owner-scoped property, found as find(owner, name); names may repeat across owners.
    const PropertyDescriptor& define(std::string owner, std::string name, PropertyValue defaultValue,
                                     PropertyFlags flags, bool inherited, PropertyKind kind);
    // Full registration with metadata. `metadata.id` and the name/owner fields are filled in.
    const PropertyDescriptor& define(PropertyDescriptor metadata);
    // A read-only property; the key is the only way to set it.
    ReadOnlyPropertyKey defineReadOnly(PropertyDescriptor metadata);
    const PropertyDescriptor* find(std::string_view name) const noexcept;
    const PropertyDescriptor* find(std::string_view owner, std::string_view name) const;

private:
    struct NameHash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const noexcept
        {
            return std::hash<std::string_view>{}(value);
        }
    };
    std::unordered_map<std::string, PropertyDescriptor, NameHash, std::equal_to<>> m_values;
    std::unordered_map<u64, std::string> m_ids; // id -> key, to reject hash collisions
};

} // namespace velora
