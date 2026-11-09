// M09 change notification primitives: Signal/Subscription lifetime and re-entrancy,
// Observable property filtering, ObservableVector notifications and item lifetime.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <velora/data/Observable.hpp>

using namespace velora;

namespace
{

int failures = 0;
void check(bool passed, const char* name)
{
    std::printf("data %s: %s\n", name, passed ? "PASS" : "FAIL");
    if (!passed)
        ++failures;
}

class Counter : public Observable
{
public:
    int value() const
    {
        return value_;
    }
    void setValue(int v)
    {
        set(value_, v, "Value");
    }
    void touchAll()
    {
        raiseAll();
    }

private:
    int value_ = 0;
};

struct Item : Observable
{
    explicit Item(int v) : value(v) {}
    ~Item()
    {
        ++destroyed;
    }
    int value;
    static inline int destroyed = 0;
};

} // namespace

int main()
{
    static_assert(propertyId("Value") == propertyId("Value") && propertyId("Value") != propertyId("Other"));
    static_assert(propertyId("") != kAllProperties);

    {
        Signal<int> signal;
        int sum = 0;
        auto a = signal.add([&](int v) { sum += v; });
        {
            auto b = signal.add([&](int v) { sum += 10 * v; });
            signal.emit(1);
        }
        signal.emit(1);
        check(sum == 12 && signal.size() == 1, "subscriptions unsubscribe when destroyed");
        Subscription moved = std::move(a);
        signal.emit(1);
        check(sum == 13 && moved.active() && !a.active(), "moving a subscription keeps it");
        moved.reset();
        signal.emit(1);
        check(sum == 13 && signal.size() == 0, "reset unsubscribes");
    }
    {
        Signal<> signal;
        std::vector<std::string> calls;
        Subscription second;
        auto first = signal.add(
            [&]
            {
                calls.push_back("first");
                second.reset(); // removed during emission: not called
            });
        second = signal.add([&] { calls.push_back("second"); });
        Subscription late;
        auto adder = signal.add(
            [&]
            {
                if (!late.active())
                    late = signal.add([&] { calls.push_back("late"); });
            });
        signal.emit();
        check(calls == std::vector<std::string>{"first"},
              "handlers removed or added during emission are not called");
        calls.clear();
        signal.emit();
        check(calls.size() == 2 && calls[1] == "late", "handlers added during emission run next time");
    }
    {
        auto owner = std::make_unique<Signal<>>();
        int calls = 0;
        auto self = owner->add(
            [&]
            {
                ++calls;
                owner.reset(); // the handler destroys the signal's owner
            });
        auto other = owner->add([&] { ++calls; });
        owner->emit();
        check(calls == 1 && !self.active() && !other.active(),
              "destroying the owner during emission is safe");
        self.reset();
        check(true, "subscriptions outliving their signal reset safely");
    }
    {
        Signal<> signal;
        Subscription self;
        int calls = 0;
        self = signal.add(
            [&]
            {
                ++calls;
                self = signal.add([&] { ++calls; }); // replaces (destroys) the running handler's token
            });
        signal.emit();
        signal.emit();
        check(calls == 2 && signal.size() == 1, "a handler may replace its own subscription while running");
    }
    {
        Counter c;
        int value = 0, any = 0;
        auto s = observe(c, propertyId("Value"), [&] { ++value; });
        auto t = observe(c, propertyId("Other"), [&] { ++any; });
        c.setValue(1);
        c.setValue(1); // unchanged: no notification
        c.touchAll();
        check(value == 2 && any == 1, "observe filters by property; all-properties reaches everyone");
        Counter copy = c;
        check(copy.subscriberCount() == 0 && c.subscriberCount() == 2, "copies do not take subscribers");
        auto none = observe(Counter{}, propertyId("Value"), [] {});
        int plain = 0;
        auto nothing = observe(plain, propertyId("Value"), [] {});
        check(!none.active() && !nothing.active(),
              "temporaries and non-observables give empty subscriptions");
        Counter* pointer = nullptr;
        check(!observe(pointer, propertyId("Value"), [] {}).active(),
              "a null pointer has nothing to observe");
    }
    {
        ObservableVector<Item> items;
        std::vector<std::string> changes;
        int seenDuringRemoval = -1;
        auto s = items.subscribe(
            [&](const VectorChange& c)
            {
                const char* kind[] = {"insert", "remove", "replace", "reset"};
                changes.push_back(std::string(kind[static_cast<int>(c.kind)]) + std::to_string(c.index));
                if (c.kind == VectorChange::Kind::kRemoved)
                    seenDuringRemoval = Item::destroyed;
            });
        Item& first = items.emplace_back(1);
        items.emplace_back(2);
        items.emplace(0, 0);
        check(&items[1] == &first && items[0].value == 0, "items keep stable addresses");
        items.erase(1);
        check(seenDuringRemoval == 0 && Item::destroyed == 1,
              "a removed item is destroyed after its notification");
        items.replace(0, 5);
        items.clear();
        check(changes == std::vector<std::string>{"insert0", "insert1", "insert0", "remove1", "replace0",
                                                  "reset0"} &&
                  Item::destroyed == 4,
              "vector notifications in order");
    }

    std::printf("data exit=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
