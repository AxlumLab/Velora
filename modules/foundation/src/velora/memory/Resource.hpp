// Allocator primitives adapted to Velora's own std::pmr memory domains.
#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <new>
#include <utility>
#include <velora/Collections.hpp>

namespace velora
{
// Small adapter for existing native allocators (including Toolkit-derived
// allocators migrated with their owners). The callback pair must support
// aligned allocation and deallocation and outlive this resource.
struct AllocatorCallbacks
{
    void* context = nullptr;
    void* (*allocate)(void*, usize, usize) = nullptr;
    void (*deallocate)(void*, void*, usize, usize) noexcept = nullptr;
};

class AllocatorResource final : public std::pmr::memory_resource
{
public:
    explicit AllocatorResource(AllocatorCallbacks callbacks);

private:
    void* do_allocate(usize bytes, usize alignment) override;
    void do_deallocate(void* pointer, usize bytes, usize alignment) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    AllocatorCallbacks m_callbacks;
};

// Explicit scope-level allocator: no changes to std::pmr::get_default_resource().
class MemoryDomain final
{
public:
    explicit MemoryDomain(std::pmr::memory_resource* upstream = std::pmr::get_default_resource());
    ~MemoryDomain();

    MemoryDomain(const MemoryDomain&) = delete;
    MemoryDomain& operator=(const MemoryDomain&) = delete;

    std::pmr::memory_resource* resource() noexcept;

private:
    std::pmr::unsynchronized_pool_resource m_pool;
};

// Per-page or per-frame scratch arena. All clients must be destroyed or
// detached before reset(); destructors are NOT called by reset().
class FrameArena final
{
public:
    explicit FrameArena(usize initialBytes = 64 * 1024,
                        std::pmr::memory_resource* upstream = std::pmr::get_default_resource());
    ~FrameArena();

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    std::pmr::memory_resource* resource() noexcept;
    void reset() noexcept;

private:
    std::pmr::monotonic_buffer_resource m_arena;
};

// Bucket-like pooling through the standard PMR small-block pools.
// Thread-safe variant for cross-thread allocations.
class BucketPool final
{
public:
    explicit BucketPool(std::pmr::pool_options options = {},
                        std::pmr::memory_resource* upstream = std::pmr::get_default_resource());
    ~BucketPool();

    BucketPool(const BucketPool&) = delete;
    BucketPool& operator=(const BucketPool&) = delete;

    std::pmr::memory_resource* resource() noexcept;
    void release() noexcept;

private:
    std::pmr::synchronized_pool_resource m_pool;
};

template <typename T, typename... Args>
std::shared_ptr<T> makeShared(std::pmr::memory_resource* resource, Args&&... args)
{
    return std::allocate_shared<T>(std::pmr::polymorphic_allocator<T>{resource}, std::forward<Args>(args)...);
}
}
