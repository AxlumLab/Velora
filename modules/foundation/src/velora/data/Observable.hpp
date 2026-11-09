#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <velora/globals.hpp>

// Change notification for view models, used by generated x:Bind code and {Binding}.
//
// A Signal hands out Subscription tokens. A token unsubscribes when destroyed, and it stays
// safe when the signal (or the object owning it) dies first: the token then does nothing.
// Handlers may subscribe, unsubscribe or destroy the signal's owner while it is emitting;
// handlers removed during an emission are not called. Copying an object copies none of its
// subscribers. Everything here is single-threaded (the UI thread).
namespace velora
{

// Property identity in notifications: a 64-bit hash of the property name, never 0.
using PropertyId = u64;
inline constexpr PropertyId kAllProperties = 0; // "every property changed"

constexpr PropertyId propertyId(std::string_view name) noexcept
{
    u64 hash = 0xcbf29ce484222325ull;
    for (const char c : name)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x100000001b3ull;
    }
    return hash == kAllProperties ? 1 : hash;
}

struct SignalChannel
{
    virtual ~SignalChannel() = default;
    virtual void remove(std::size_t slot) noexcept = 0;
};

class Subscription
{
public:
    Subscription() = default;
    Subscription(std::weak_ptr<SignalChannel> channel, std::size_t slot)
        : m_channel(std::move(channel)), m_slot(slot)
    {
    }
    Subscription(Subscription&& other) noexcept
        : m_channel(std::move(other.m_channel)), m_slot(std::exchange(other.m_slot, 0))
    {
    }
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_channel = std::move(other.m_channel);
            m_slot = std::exchange(other.m_slot, 0);
        }
        return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription()
    {
        reset();
    }

    void reset() noexcept
    {
        if (const auto channel = m_channel.lock())
            channel->remove(m_slot);
        m_channel.reset();
        m_slot = 0;
    }
    // True while the signal is alive and this token is subscribed.
    bool active() const noexcept
    {
        return m_slot != 0 && !m_channel.expired();
    }

private:
    std::weak_ptr<SignalChannel> m_channel;
    std::size_t m_slot = 0;
};

template <class... Args> class Signal
{
public:
    using Handler = std::function<void(Args...)>;

    Signal() = default;
    Signal(const Signal&) {}
    Signal& operator=(const Signal&)
    {
        return *this;
    }
    // Moving keeps the subscribers with the moved-from object's address semantics: none move.
    Signal(Signal&&) noexcept {}
    Signal& operator=(Signal&&) noexcept
    {
        return *this;
    }
    // Handlers still pending in an emission that destroys this signal are not called.
    ~Signal()
    {
        if (!m_channel)
            return;
        for (auto& e : m_channel->entries)
            e.entry->alive = false;
        m_channel->entries.clear();
    }

    Subscription add(Handler handler) const
    {
        auto& c = channel();
        const std::size_t slot = c.next++;
        c.entries.push_back({slot, std::make_shared<Entry>(Entry{std::move(handler)})});
        return Subscription(m_channel, slot);
    }

    void emit(Args... args) const
    {
        if (!m_channel)
            return;
        const auto keep = m_channel; // a handler may destroy the owner of this signal
        std::vector<std::shared_ptr<Entry>> snapshot;
        snapshot.reserve(keep->entries.size());
        for (const auto& e : keep->entries)
            snapshot.push_back(e.entry);
        for (const auto& entry : snapshot)
            if (entry->alive)
                entry->handler(args...);
    }

    std::size_t size() const noexcept
    {
        return m_channel ? m_channel->entries.size() : 0;
    }

private:
    struct Entry
    {
        Handler handler;
        bool alive = true;
    };
    struct Channel final : SignalChannel
    {
        struct Slot
        {
            std::size_t slot;
            std::shared_ptr<Entry> entry;
        };
        std::vector<Slot> entries;
        std::size_t next = 1;
        void remove(std::size_t slot) noexcept override
        {
            for (auto it = entries.begin(); it != entries.end(); ++it)
                if (it->slot == slot)
                {
                    it->entry->alive = false;
                    entries.erase(it);
                    return;
                }
        }
    };
    Channel& channel() const
    {
        if (!m_channel)
            m_channel = std::make_shared<Channel>();
        return *m_channel;
    }
    mutable std::shared_ptr<Channel> m_channel;
};

// Base class of objects that report property changes (INotifyPropertyChanged).
class Observable
{
public:
    using Handler = std::function<void(PropertyId)>;
    Subscription subscribe(Handler handler) const
    {
        return m_changed.add(std::move(handler));
    }
    std::size_t subscriberCount() const noexcept
    {
        return m_changed.size();
    }

protected:
    Observable() = default;
    ~Observable() = default;
    void raise(PropertyId id) const
    {
        m_changed.emit(id);
    }
    void raise(std::string_view name) const
    {
        raise(propertyId(name));
    }
    void raiseAll() const
    {
        raise(kAllProperties);
    }
    // Assigns and raises `name` when the value changes; returns whether it changed.
    template <class T, class U> bool set(T& field, U&& value, std::string_view name)
    {
        if (field == value)
            return false;
        field = std::forward<U>(value);
        raise(name);
        return true;
    }

private:
    Signal<PropertyId> m_changed;
};

// Subscribes `handler` to property `id` of `source` when it is an Observable; otherwise, and
// for temporaries (nothing to observe), returns an empty subscription.
template <class T, class F> Subscription observe(T& source, PropertyId id, F&& handler)
{
    if constexpr (std::is_base_of_v<Observable, std::remove_cv_t<T>>)
    {
        return static_cast<const Observable&>(source).subscribe(
            [id, handler = std::forward<F>(handler)](PropertyId changed)
            {
                if (changed == id || changed == kAllProperties)
                    handler();
            });
    }
    else
    {
        return {};
    }
}
template <class T, class F>
    requires(!std::is_lvalue_reference_v<T>)
Subscription observe(T&&, PropertyId, F&&)
{
    return {};
}
// Pointer steps observe the pointee; a null pointer has nothing to observe.
template <class T, class F> Subscription observe(T* source, PropertyId id, F&& handler)
{
    return source ? observe(*source, id, std::forward<F>(handler)) : Subscription{};
}

struct VectorChange
{
    enum class Kind
    {
        kInserted,
        kRemoved,
        kReplaced,
        kReset
    } kind;
    std::size_t index = 0;
};

// Vector with change notification (IObservableVector). Items have stable addresses; a
// removed item is destroyed only after its Removed notification has been delivered.
template <class T> class ObservableVector
{
public:
    using Handler = std::function<void(const VectorChange&)>;

    ObservableVector() = default;
    ObservableVector(const ObservableVector&) = delete;
    ObservableVector& operator=(const ObservableVector&) = delete;

    std::size_t size() const noexcept
    {
        return m_items.size();
    }
    bool empty() const noexcept
    {
        return m_items.empty();
    }
    T& operator[](std::size_t index)
    {
        return *m_items[index];
    }
    const T& operator[](std::size_t index) const
    {
        return *m_items[index];
    }

    template <class... A> T& emplace(std::size_t index, A&&... args)
    {
        auto& item = **m_items.insert(m_items.begin() + static_cast<std::ptrdiff_t>(index),
                                      std::make_unique<T>(std::forward<A>(args)...));
        m_changed.emit({VectorChange::Kind::kInserted, index});
        return item;
    }
    template <class... A> T& emplace_back(A&&... args)
    {
        return emplace(m_items.size(), std::forward<A>(args)...);
    }
    void erase(std::size_t index)
    {
        auto removed = std::move(m_items[index]);
        m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));
        m_changed.emit({VectorChange::Kind::kRemoved, index});
    }
    template <class... A> T& replace(std::size_t index, A&&... args)
    {
        auto removed = std::exchange(m_items[index], std::make_unique<T>(std::forward<A>(args)...));
        m_changed.emit({VectorChange::Kind::kReplaced, index});
        return *m_items[index];
    }
    void clear()
    {
        auto removed = std::move(m_items);
        m_items.clear();
        m_changed.emit({VectorChange::Kind::kReset, 0});
    }

    Subscription subscribe(Handler handler) const
    {
        return m_changed.add(std::move(handler));
    }
    std::size_t subscriberCount() const noexcept
    {
        return m_changed.size();
    }

private:
    std::vector<std::unique_ptr<T>> m_items;
    Signal<const VectorChange&> m_changed;
};

} // namespace velora
