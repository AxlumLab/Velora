#include <stdexcept>
#include <velora/memory/Resource.hpp>

namespace velora
{
AllocatorResource::AllocatorResource(AllocatorCallbacks callbacks) : m_callbacks(callbacks)
{
    if (!callbacks.allocate || !callbacks.deallocate)
    {
        throw std::invalid_argument("both allocation callbacks are required");
    }
}

void* AllocatorResource::do_allocate(usize bytes, usize alignment)
{
    void* result = m_callbacks.allocate(m_callbacks.context, bytes, alignment);
    if (!result)
    {
        throw std::bad_alloc{};
    }
    return result;
}

void AllocatorResource::do_deallocate(void* pointer, usize bytes, usize alignment)
{
    m_callbacks.deallocate(m_callbacks.context, pointer, bytes, alignment);
}

bool AllocatorResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

MemoryDomain::MemoryDomain(std::pmr::memory_resource* upstream) : m_pool(upstream) {}

MemoryDomain::~MemoryDomain() = default;

std::pmr::memory_resource* MemoryDomain::resource() noexcept
{
    return &m_pool;
}

FrameArena::FrameArena(usize initialBytes, std::pmr::memory_resource* upstream)
    : m_arena(initialBytes, upstream)
{
}

FrameArena::~FrameArena() = default;

std::pmr::memory_resource* FrameArena::resource() noexcept
{
    return &m_arena;
}

void FrameArena::reset() noexcept
{
    m_arena.release();
}

BucketPool::BucketPool(std::pmr::pool_options options, std::pmr::memory_resource* upstream)
    : m_pool(options, upstream)
{
}

BucketPool::~BucketPool() = default;

std::pmr::memory_resource* BucketPool::resource() noexcept
{
    return &m_pool;
}

void BucketPool::release() noexcept
{
    m_pool.release();
}
}
