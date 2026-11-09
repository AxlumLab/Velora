// Adapted from Toolkit CoreKit StackAllocator; a safe, single-slot std::pmr resource.
// Copyright (c) 2026 FlybirdGames. All rights reserved.
#pragma once

#include <cassert>
#include <cstddef>
#include <memory_resource>
#include <new>
#include <velora/globals.hpp>

namespace velora
{
// Only one small allocation uses the inline slot at once; all others go to upstream.
// Not thread safe. Clients must release all allocations before this resource is destroyed.
template <usize Capacity, usize Alignment = alignof(std::max_align_t)>
class StackResource final : public std::pmr::memory_resource
{
public:
    explicit StackResource(std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
        : m_upstream(upstream)
    {
        static_assert(Capacity > 0);
        static_assert(Alignment != 0 && (Alignment & (Alignment - 1)) == 0);
        assert(m_upstream != nullptr);
    }

    ~StackResource() override
    {
        assert(!m_inUse);
    }

    StackResource(const StackResource&) = delete;
    StackResource& operator=(const StackResource&) = delete;

private:
    void* do_allocate(usize bytes, usize alignment) override
    {
        if (!m_inUse && bytes <= Capacity && alignment <= Alignment)
        {
            m_inUse = true;
            return m_storage;
        }
        return m_upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, usize bytes, usize alignment) override
    {
        if (pointer == m_storage)
        {
            assert(m_inUse);
            m_inUse = false;
            return;
        }
        m_upstream->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }

    alignas(Alignment) std::byte m_storage[Capacity];
    std::pmr::memory_resource* m_upstream;
    bool m_inUse = false;
};
}
