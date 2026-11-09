#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <velora/globals.hpp>

namespace velora
{

class ReflectError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

enum class ReflectValueKind
{
    kEmpty,
    kBool,
    kI64,
    kU64,
    kF64,
    kString,
    kPointer
};

class ReflectValue
{
    using Storage = std::variant<std::monostate, bool, i64, u64, double, std::string, const void*>;
    Storage value_;

public:
    ReflectValue() = default;
    ReflectValue(bool value) : value_(value) {}
    ReflectValue(i64 value) : value_(value) {}
    ReflectValue(u64 value) : value_(value) {}
    ReflectValue(double value) : value_(value) {}
    ReflectValue(std::string value) : value_(std::move(value)) {}
    ReflectValue(const char* value) : value_(std::string(value ? value : "")) {}
    ReflectValue(const void* value) : value_(value) {}

    ReflectValueKind kind() const noexcept
    {
        return static_cast<ReflectValueKind>(value_.index());
    }
    i64 asI64() const
    {
        if (const auto* value = std::get_if<i64>(&value_))
            return *value;
        if (const auto* value = std::get_if<u64>(&value_))
        {
            if (*value > static_cast<u64>(std::numeric_limits<i64>::max()))
                throw ReflectError("integer overflow");
            return static_cast<i64>(*value);
        }
        throw ReflectError("value is not an integer");
    }
    std::string_view asString() const
    {
        if (const auto* value = std::get_if<std::string>(&value_))
            return *value;
        throw ReflectError("value is not a string");
    }
    bool asBool() const
    {
        if (auto v = std::get_if<bool>(&value_))
            return *v;
        throw ReflectError("value is not bool");
    }
    u64 asU64() const
    {
        if (auto v = std::get_if<u64>(&value_))
            return *v;
        throw ReflectError("value is not unsigned integer");
    }
    double asF64() const
    {
        if (auto v = std::get_if<double>(&value_))
            return *v;
        throw ReflectError("value is not double");
    }
};

std::string serializeValue(const ReflectValue& value);
ReflectValue deserializeValue(std::string_view text);
// This identifies the reflection registration API; it does not promise binary compatibility.
u32 runtimeApiVersion() noexcept;

struct EnumDescriptor
{
    std::string fullName;
    std::vector<std::pair<std::string, i64>> values;
    i64 value(std::string_view key) const;
    std::string_view name(i64 value) const;
};

struct FieldDescriptor
{
    std::string name;
    ReflectValueKind kind = ReflectValueKind::kEmpty;
    std::function<ReflectValue(const void*)> getter;
    std::function<void(void*, const ReflectValue&)> setter;
    bool serializable = true;

    ReflectValue get(const void* object) const
    {
        if (!object || !getter)
            throw ReflectError("field getter called with invalid object");
        auto value = getter(object);
        if (value.kind() != kind)
            throw ReflectError("field getter type mismatch");
        return value;
    }
    void set(void* object, const ReflectValue& value) const
    {
        if (!object || !setter)
            throw ReflectError("field is read-only or object is invalid");
        if (value.kind() != kind)
            throw ReflectError("field type mismatch");
        setter(object, value);
    }
};

struct MethodDescriptor
{
    std::string name;
    std::size_t arity = 0;
    std::function<ReflectValue(void*, std::span<const ReflectValue>)> invoker;
    std::vector<ReflectValueKind> parameters;

    ReflectValue invoke(void* object, std::span<const ReflectValue> args) const
    {
        if (!object || !invoker)
            throw ReflectError("method called with invalid object");
        if (args.size() != arity)
            throw ReflectError("method argument count mismatch");
        if (!parameters.empty())
        {
            if (parameters.size() != arity)
                throw ReflectError("invalid method descriptor");
            for (std::size_t i = 0; i < arity; ++i)
                if (parameters[i] != args[i].kind())
                    throw ReflectError("method argument type mismatch");
        }
        return invoker(object, args);
    }
};

struct TypeDescriptor
{
    std::string name;
    std::string fullName;
    std::size_t size = 0;
    std::size_t alignment = 0;
    std::unordered_map<std::string, FieldDescriptor> fields;
    std::unordered_map<std::string, MethodDescriptor> methods;
    std::shared_ptr<const TypeDescriptor> base;
    std::function<void*(void*)> baseCast;
    std::function<const void*(const void*)> constBaseCast;

    template <class Derived, class Base> void setBase(std::shared_ptr<const TypeDescriptor> descriptor)
    {
        if (!descriptor || descriptor.get() == this)
            throw ReflectError("invalid base descriptor");
        for (auto p = descriptor; p; p = p->base)
            if (p.get() == this)
                throw ReflectError("inheritance cycle");
        base = std::move(descriptor);
        baseCast = [](void* p) -> void* { return static_cast<Base*>(static_cast<Derived*>(p)); };
        constBaseCast = [](const void* p) -> const void*
        { return static_cast<const Base*>(static_cast<const Derived*>(p)); };
    }
    ReflectValue getField(const void* object, std::string_view key) const;
    void setField(void* object, std::string_view key, const ReflectValue& value) const;
    ReflectValue invoke(void* object, std::string_view key, std::span<const ReflectValue> args) const;
    std::string serialize(const void* object) const;
    void deserialize(void* object, std::string_view text) const;

    const FieldDescriptor& field(std::string_view key) const
    {
        auto it = fields.find(std::string(key));
        if (it == fields.end())
            throw ReflectError("unknown reflected field");
        return it->second;
    }
    const MethodDescriptor& method(std::string_view key) const
    {
        auto it = methods.find(std::string(key));
        if (it == methods.end())
            throw ReflectError("unknown reflected method");
        return it->second;
    }
};

class TypeRegistry
{
    std::unordered_map<std::string, std::shared_ptr<TypeDescriptor>> m_types;

public:
    void add(std::shared_ptr<TypeDescriptor> type)
    {
        if (!type || type->fullName.empty())
            throw ReflectError("invalid type descriptor");
        if (!m_types.emplace(type->fullName, std::move(type)).second)
            throw ReflectError("duplicate reflected type");
    }
    void remove(std::string_view fullName)
    {
        m_types.erase(std::string(fullName));
    }
    std::shared_ptr<TypeDescriptor> find(std::string_view fullName) const
    {
        auto it = m_types.find(std::string(fullName));
        return it == m_types.end() ? nullptr : it->second;
    }
};

class ReflectModule
{
    TypeRegistry& m_registry;
    std::string m_name;
    std::vector<std::string> m_types;
    bool m_loaded = true;

public:
    ReflectModule(TypeRegistry& registry, std::string name, u32 apiVersion = 1)
        : m_registry(registry), m_name(std::move(name))
    {
        if (apiVersion != runtimeApiVersion())
            throw ReflectError("reflection API version mismatch");
    }
    ReflectModule(const ReflectModule&) = delete;
    ReflectModule& operator=(const ReflectModule&) = delete;
    void add(const std::shared_ptr<TypeDescriptor>& type)
    {
        if (!m_loaded)
            throw ReflectError("module is unloaded");
        if (!type)
            throw ReflectError("invalid type descriptor");
        m_types.push_back(type->fullName);
        try
        {
            m_registry.add(type);
        }
        catch (...)
        {
            m_types.pop_back();
            throw;
        }
    }
    void unload()
    {
        if (!m_loaded)
            return;
        for (const auto& type : m_types)
            m_registry.remove(type);
        m_loaded = false;
    }
    ~ReflectModule()
    {
        unload();
    }
};

struct ObjectHandle
{
    u32 slot = 0;
    u32 generation = 0;
    friend bool operator==(ObjectHandle, ObjectHandle) = default;
};

class ObjectRegistry
{
    struct Entry
    {
        std::unique_ptr<void, void (*)(void*)> object{nullptr, [](void*) {}};
        std::shared_ptr<TypeDescriptor> type;
        u32 generation = 1;
        std::type_index cppType{typeid(void)};
    };
    std::vector<Entry> m_entries;

public:
    template <typename T, typename... Args>
    ObjectHandle emplace(std::shared_ptr<TypeDescriptor> type, Args&&... args)
    {
        if (!type || type->size != sizeof(T) || type->alignment != alignof(T))
            throw ReflectError("object descriptor layout mismatch");
        auto destroy = [](void* pointer) { delete static_cast<T*>(pointer); };
        auto object = std::unique_ptr<void, void (*)(void*)>(new T(std::forward<Args>(args)...), destroy);
        for (u32 i = 0; i < m_entries.size(); ++i)
        {
            if (!m_entries[i].object)
            {
                const auto generation = ++m_entries[i].generation;
                m_entries[i] = {std::move(object), std::move(type), generation, typeid(T)};
                return {i, generation};
            }
        }
        m_entries.push_back({std::move(object), std::move(type), 1, typeid(T)});
        return {static_cast<u32>(m_entries.size() - 1), 1};
    }

    template <typename T> T* resolve(ObjectHandle handle) const noexcept
    {
        if (handle.slot >= m_entries.size())
            return nullptr;
        const auto& entry = m_entries[handle.slot];
        if (!entry.object || entry.generation != handle.generation || entry.cppType != typeid(T))
            return nullptr;
        return static_cast<T*>(entry.object.get());
    }
    void erase(ObjectHandle handle) noexcept
    {
        if (handle.slot >= m_entries.size())
            return;
        auto& entry = m_entries[handle.slot];
        if (entry.object && entry.generation == handle.generation)
        {
            entry.object.reset();
            entry.type.reset();
        }
    }
};

} // namespace velora
