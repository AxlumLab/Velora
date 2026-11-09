#include <cstdlib>
#include <iostream>
#include <limits>
#include <velora/globals.hpp>
#include <velora/reflect/Annotations.hpp>
#include <velora/reflect/Reflect.hpp>

using namespace velora;
void check(bool condition)
{
    if (!condition)
    {
        std::cerr << "contract failed\n";
        std::exit(1);
    }
}
template <class F> void rejects(F operation)
{
    bool caught = false;
    try
    {
        operation();
    }
    catch (const ReflectError&)
    {
        caught = true;
    }
    check(caught);
}
struct Base
{
    velora::i64 value = 4;
};
struct Padding
{
    virtual ~Padding() = default;
    int padding = 1;
};
struct Derived : Padding, Base
{
};
rkstruct Annotated
{
    R_STRUCT(Annotated) rkfield() velora::i64 number = 0;
};
int main()
{
    rejects([] { ReflectValue(std::numeric_limits<velora::u64>::max()).asI64(); });
    rejects([] { deserializeValue("value 99 x"); });
    for (const ReflectValue& v : {ReflectValue{}, ReflectValue(true), ReflectValue(velora::i64{-42}),
                                  ReflectValue(std::numeric_limits<velora::u64>::max()), ReflectValue(1.25),
                                  ReflectValue(std::string("quoted \" text\n\0end", 19))})
        check(serializeValue(deserializeValue(serializeValue(v))) == serializeValue(v));
    rejects([] { serializeValue(ReflectValue(static_cast<const void*>(nullptr))); });
    EnumDescriptor enumeration{"Mode", {{"Off", 0}, {"On", 1}}};
    check(enumeration.value("On") == 1 && enumeration.name(0) == "Off");
    rejects([&] { enumeration.value("Missing"); });
    auto base = std::make_shared<TypeDescriptor>("Base", "test::Base", sizeof(Base), alignof(Base));
    base->fields.emplace("value",
                         FieldDescriptor{"value", ReflectValueKind::kI64, [](const void* p)
                                         { return ReflectValue(static_cast<const Base*>(p)->value); },
                                         [](void* p, const ReflectValue& v)
                                         { static_cast<Base*>(p)->value = v.asI64(); }});
    auto derived =
        std::make_shared<TypeDescriptor>("Derived", "test::Derived", sizeof(Derived), alignof(Derived));
    derived->setBase<Derived, Base>(base);
    Derived object;
    derived->setField(&object, "value", ReflectValue(velora::i64{8}));
    check(derived->getField(&object, "value").asI64() == 8);
    check(Annotated::staticType().size == sizeof(Annotated));
    const auto snapshot = derived->serialize(&object);
    object.value = 0;
    derived->deserialize(&object, snapshot);
    check(object.value == 8);
    rejects([&] { derived->deserialize(&object, snapshot + "\"value\" value 1 2 99\n"); });
    check(object.value == 8);
    rejects([&] { derived->deserialize(&object, "reflect-object 1 \"wrong\"\n"); });
    derived->methods.emplace("set(int)", MethodDescriptor{"set",
                                                          1,
                                                          [](void* p, std::span<const ReflectValue> a)
                                                          {
                                                              static_cast<Derived*>(p)->value = a[0].asI64();
                                                              return ReflectValue{};
                                                          },
                                                          {ReflectValueKind::kI64}});
    derived->methods.emplace("set(string)", MethodDescriptor{"set",
                                                             1,
                                                             [](void*, std::span<const ReflectValue>)
                                                             { return ReflectValue("text"); },
                                                             {ReflectValueKind::kString}});
    const ReflectValue argument(velora::i64{9});
    derived->invoke(&object, "set", std::span(&argument, 1));
    check(object.value == 9);
    rejects([&] { derived->setField(&object, "value", ReflectValue("bad")); });
    TypeRegistry registry;
    rejects([&] { ReflectModule bad(registry, "bad", runtimeApiVersion() + 1); });
    {
        ReflectModule module(registry, "good", runtimeApiVersion());
        module.add(derived);
        check(registry.find("test::Derived") != nullptr);
    }
    check(!registry.find("test::Derived"));
    ObjectRegistry objects;
    const auto handle = objects.emplace<Derived>(derived);
    check(objects.resolve<Base>(handle) == nullptr);
    objects.erase(handle);
    check(objects.resolve<Derived>(handle) == nullptr);
    std::cout << "Reflection value/serialization/enum/inheritance/overload/API/typed-handle contracts passed\n";
}
