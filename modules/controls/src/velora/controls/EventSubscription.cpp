#include <exception>
#include <utility>
#include <velora/controls/EventSubscription.hpp>
#include <velora/runtime/Framework.hpp>

namespace velora
{
EventSubscription::EventSubscription(Framework& owner, u64 element, u64 token) noexcept
    : m_owner(&owner), m_lifetime(owner.m_lifetime), m_element(element), m_token(token)
{
}

EventSubscription::~EventSubscription()
{
    reset();
}

EventSubscription::EventSubscription(EventSubscription&& other) noexcept
    : m_owner(std::exchange(other.m_owner, nullptr)), m_lifetime(std::move(other.m_lifetime)),
      m_element(std::exchange(other.m_element, 0)), m_token(std::exchange(other.m_token, 0))
{
}

EventSubscription& EventSubscription::operator=(EventSubscription&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_owner = std::exchange(other.m_owner, nullptr);
        m_lifetime = std::move(other.m_lifetime);
        m_element = std::exchange(other.m_element, 0);
        m_token = std::exchange(other.m_token, 0);
    }
    return *this;
}

void EventSubscription::reset() noexcept
{
    if (!m_owner)
        return;
    // UI-thread destruction is part of the contract; silently leaking a live registration on
    // another thread would leave callbacks observing objects whose scope has already ended.
    const auto lifetime = m_lifetime.lock();
    const bool alive = lifetime && *lifetime;
    if (alive && !m_owner->isOwnerThread())
        std::terminate();
    auto* owner = std::exchange(m_owner, nullptr);
    const auto element = std::exchange(m_element, 0);
    const auto token = std::exchange(m_token, 0);
    m_lifetime.reset();
    if (alive)
        owner->removeHandler(element, token);
}
} // namespace velora
