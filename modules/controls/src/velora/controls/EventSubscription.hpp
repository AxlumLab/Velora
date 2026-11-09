#pragma once

#include <memory>
#include <velora/globals.hpp>

namespace velora
{
class Framework;

// Owns one routed-event registration, not the element or Framework. Move, reset and destruction
// must run on the UI owner thread while the Framework is alive. May outlive either target.
class EventSubscription
{
  public:
    EventSubscription() noexcept = default;
    ~EventSubscription();
    EventSubscription(EventSubscription&& other) noexcept;
    EventSubscription& operator=(EventSubscription&& other) noexcept;
    EventSubscription(const EventSubscription&) = delete;
    EventSubscription& operator=(const EventSubscription&) = delete;

    void reset() noexcept;

  private:
    friend class Framework;
    EventSubscription(Framework& owner, u64 element, u64 token) noexcept;
    Framework* m_owner = nullptr;
    std::weak_ptr<bool> m_lifetime;
    u64 m_element = 0;
    u64 m_token = 0;
};
} // namespace velora
