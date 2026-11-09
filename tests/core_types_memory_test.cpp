#include <cassert>
#include <cstdint>
#include <memory_resource>
#include <string>
#include <type_traits>
#include <velora/Collections.hpp>
#include <velora/environment.hpp>
#include <velora/globals.hpp>
#include <velora/io/BinaryStream.hpp>
#include <velora/memory/StackResource.hpp>
#include <velora/memory/Resource.hpp>

int main()
{
    static_assert(sizeof(velora::u64) == 8);
    static_assert(sizeof(velora::usize) == sizeof(void*));
    static_assert(VELORA_VERSION_MAJOR == 0);
    static_assert(VELORA_PLATFORM_WINDOWS + VELORA_PLATFORM_LINUX + VELORA_PLATFORM_MACOS == 1);

    struct CallbackStats
    {
        int allocations = 0;
        int deallocations = 0;
    } stats;
    velora::AllocatorCallbacks callbacks;
    callbacks.context = &stats;
    callbacks.allocate = [](void* context, velora::usize bytes, velora::usize alignment) -> void*
    {
        ++static_cast<CallbackStats*>(context)->allocations;
        return ::operator new(bytes, std::align_val_t(alignment));
    };
    callbacks.deallocate = [](void* context, void* ptr, velora::usize, velora::usize alignment) noexcept
    {
        ++static_cast<CallbackStats*>(context)->deallocations;
        ::operator delete(ptr, std::align_val_t(alignment));
    };
    {
        velora::AllocatorResource bridge(callbacks);
        velora::Array<int> external{&bridge};
        external.push_back(9);
        assert(external[0] == 9);
    }
    assert(stats.allocations > 0 && stats.allocations == stats.deallocations);

    velora::MemoryDomain domain;
    velora::Array<int> values{domain.resource()};
    values.push_back(3);
    values.push_back(7);
    assert(values.size() == 2 && values[1] == 7);

    velora::HashMap<velora::u32, velora::String> lookup{domain.resource()};
    lookup.emplace(42, velora::String("hello", domain.resource()));
    assert(lookup.at(42) == "hello");

    velora::BucketPool bucket;
    velora::Array<int> bucketValues{bucket.resource()};
    bucketValues.resize(200);
    assert(bucketValues[199] == 0);

    velora::FrameArena frame;
    {
        velora::Array<velora::u8> scratch{frame.resource()};
        scratch.assign(512, 2);
        assert(scratch[511] == 2);
    }
    frame.reset();

    // Toolkit MemoryStream port: little-endian and truncation rejection.
    velora::BinaryWriter writer;
    writer.u16v(0xBEEF);
    writer.u64v(0x0102030405060708ULL);
    writer.text("Hello");
    velora::BinaryReader reader(writer.bytes());
    assert(reader.u16v() == 0xBEEF);
    assert(reader.u64v() == 0x0102030405060708ULL);
    assert(reader.text() == "Hello" && reader.atEnd() && reader.ok());
    assert(reader.u32v() == 0 && !reader.ok());
    // Toolkit StackAllocator port: small local allocation and safe upstream fallback.
    velora::StackResource<256> smallResource;
    {
        std::pmr::vector<int> fast{&smallResource};
        fast.push_back(17);
        assert(fast[0] == 17);
        std::pmr::vector<int> fallback{&smallResource};
        fallback.resize(512);
        assert(fallback.size() == 512);
    }

    auto value = velora::makeShared<std::string>(domain.resource(), "RAII");
    assert(*value == "RAII");
}
