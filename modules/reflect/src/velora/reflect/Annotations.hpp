#pragma once
#include "velora/reflect/Markers.hpp"
#include "velora/reflect/Reflect.hpp"

// Source annotations remain usable without inheriting a reflection base.

namespace velora
{
template <class T> std::shared_ptr<TypeDescriptor>& annotatedType()
{
    static auto descriptor = std::make_shared<TypeDescriptor>("", "", sizeof(T), alignof(T));
    return descriptor;
}
}
#define R_OBJECT(Type)                                                                                       \
public:                                                                                                      \
    static const ::velora::TypeDescriptor& staticType()                                                      \
    {                                                                                                        \
        return *::velora::annotatedType<Type>();                                                             \
    }                                                                                                        \
    static const ::velora::TypeDescriptor& staticClass()                                                     \
    {                                                                                                        \
        return staticType();                                                                                 \
    }                                                                                                        \
    static auto staticTypeHandle()                                                                           \
    {                                                                                                        \
        return ::velora::annotatedType<Type>();                                                              \
    }                                                                                                        \
    const ::velora::TypeDescriptor& getClass() const                                                         \
    {                                                                                                        \
        return staticType();                                                                                 \
    }                                                                                                        \
                                                                                                             \
private:                                                                                                     \
    friend struct Type##_ReflectAccess;                                                                      \
                                                                                                             \
public:
#define R_STRUCT(Type)                                                                                       \
    R_OBJECT(Type)                                                                                           \
    static const ::velora::TypeDescriptor& staticStruct()                                                    \
    {                                                                                                        \
        return staticType();                                                                                 \
    }
#define R_OBJECT_NO_SPAWN(Type) R_OBJECT(Type)
