#include <array>
#include <cassert>
#include <string>
#include <velora/globals.hpp>
#include <velora/reflect/Reflect.hpp>

struct Counter
{
    int value = 2;
    std::string label = "counter";
    int add(int amount)
    {
        value += amount;
        return value;
    }
};

int main()
{
    using namespace velora;
    TypeRegistry registry;
    ReflectModule module(registry, "consumer");
    auto type =
        std::make_shared<TypeDescriptor>("Counter", "demo::Counter", sizeof(Counter), alignof(Counter));
    type->fields.emplace(
        "value",
        FieldDescriptor{
            "value", ReflectValueKind::kI64, [](const void* p)
            { return ReflectValue(static_cast<velora::i64>(static_cast<const Counter*>(p)->value)); },
            [](void* p, const ReflectValue& v)
            { static_cast<Counter*>(p)->value = static_cast<int>(v.asI64()); }});
    type->methods.emplace(
        "add", MethodDescriptor{"add", 1, [](void* p, std::span<const ReflectValue> args)
                                {
                                    return ReflectValue(static_cast<velora::i64>(
                                        static_cast<Counter*>(p)->add(static_cast<int>(args[0].asI64()))));
                                }});
    module.add(type);
    assert(registry.find("demo::Counter") == type);
    Counter counter;
    type->field("value").set(&counter, ReflectValue(velora::i64{7}));
    assert(type->field("value").get(&counter).asI64() == 7);
    const std::array<ReflectValue, 1> addArgs{ReflectValue(velora::i64{3})};
    assert(type->method("add").invoke(&counter, addArgs).asI64() == 10);

    ObjectRegistry objects;
    auto handle = objects.emplace<Counter>(type, counter);
    assert(objects.resolve<Counter>(handle)->value == 10);
    objects.erase(handle);
    assert(!objects.resolve<Counter>(handle));
    auto next = objects.emplace<Counter>(type, Counter{});
    assert(next.slot == handle.slot && next.generation != handle.generation);

    module.unload();
    assert(!registry.find("demo::Counter"));
    bool rejected = false;
    try
    {
        (void)type->field("value").get(nullptr);
    }
    catch (const ReflectError&)
    {
        rejected = true;
    }
    assert(rejected);
    return 0;
}
