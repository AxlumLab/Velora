#pragma once

#include <memory>
#include <typeinfo>
#include <utility>

namespace velora
{

// Reference to an application object with its dynamic type, e.g. a DataContext or a reflected
// object reached through accessors. A borrowed reference requires the object to outlive every
// holder; a shared one keeps it alive. Equal when both name the same object.
struct ObjectRef
{
    void* object = nullptr;
    const std::type_info* type = nullptr;
    std::shared_ptr<void> owner;

    template <class T> static ObjectRef borrow(T& object)
    {
        return {const_cast<void*>(static_cast<const void*>(&object)), &typeid(T), nullptr};
    }
    template <class T> static ObjectRef share(std::shared_ptr<T> object)
    {
        void* raw = const_cast<void*>(static_cast<const void*>(object.get()));
        return {raw, &typeid(T), std::move(object)};
    }
    explicit operator bool() const noexcept
    {
        return object != nullptr;
    }
    friend bool operator==(const ObjectRef& a, const ObjectRef& b) noexcept
    {
        return a.object == b.object && (a.type == b.type || (a.type && b.type && *a.type == *b.type));
    }
};

} // namespace velora
