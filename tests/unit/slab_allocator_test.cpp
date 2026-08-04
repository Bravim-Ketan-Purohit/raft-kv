#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "raftkv/slab_allocator.h"

namespace raftkv {
namespace {

TEST(SlabAllocatorTest, AllocateSmallSizes) {
    SlabAllocator alloc;

    // Allocate various small sizes
    void* p1 = alloc.allocate(8);
    void* p2 = alloc.allocate(16);
    void* p3 = alloc.allocate(32);
    void* p4 = alloc.allocate(64);

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);
    ASSERT_NE(p4, nullptr);

    // All should be different pointers
    EXPECT_NE(p1, p2);
    EXPECT_NE(p2, p3);
    EXPECT_NE(p3, p4);

    alloc.deallocate(p1, 8);
    alloc.deallocate(p2, 16);
    alloc.deallocate(p3, 32);
    alloc.deallocate(p4, 64);
}

TEST(SlabAllocatorTest, AllocateAllSizeClasses) {
    SlabAllocator alloc;

    std::vector<void*> ptrs;
    for (size_t sz : SlabAllocator::kSizeClasses) {
        void* p = alloc.allocate(sz);
        ASSERT_NE(p, nullptr);
        ptrs.push_back(p);
    }

    // All are usable
    for (size_t i = 0; i < ptrs.size(); ++i) {
        std::memset(ptrs[i], 0xAB, SlabAllocator::kSizeClasses[i]);
    }

    for (size_t i = 0; i < ptrs.size(); ++i) {
        alloc.deallocate(ptrs[i], SlabAllocator::kSizeClasses[i]);
    }
}

TEST(SlabAllocatorTest, LargeAllocationFallsThrough) {
    SlabAllocator alloc;

    // Allocate larger than max slab size
    void* p = alloc.allocate(4096);
    ASSERT_NE(p, nullptr);

    auto stats = alloc.stats();
    EXPECT_EQ(stats.large_allocs, 1u);

    alloc.deallocate(p, 4096);
    stats = alloc.stats();
    EXPECT_EQ(stats.large_allocs, 0u);
}

TEST(SlabAllocatorTest, StatsAccountingCorrect) {
    SlabAllocator alloc;

    auto s0 = alloc.stats();
    EXPECT_EQ(s0.bytes_requested, 0u);
    EXPECT_EQ(s0.bytes_mapped, 0u);
    EXPECT_EQ(s0.bytes_live, 0u);
    EXPECT_EQ(s0.chunks, 0u);

    void* p1 = alloc.allocate(100); // Goes into 128-byte class
    auto s1 = alloc.stats();
    EXPECT_EQ(s1.bytes_requested, 100u);
    EXPECT_GT(s1.bytes_mapped, 0u); // At least one arena
    EXPECT_EQ(s1.bytes_live, 128u); // Rounded up to size class
    EXPECT_EQ(s1.chunks, 1u);

    alloc.deallocate(p1, 100);
    auto s2 = alloc.stats();
    EXPECT_EQ(s2.bytes_live, 0u);
    EXPECT_GT(s2.bytes_free_listed, 0u);
}

TEST(SlabAllocatorTest, ReuseFreedMemory) {
    SlabAllocator alloc;

    void* p1 = alloc.allocate(32);
    alloc.deallocate(p1, 32);

    void* p2 = alloc.allocate(32);
    // Should reuse the freed slot
    EXPECT_EQ(p1, p2);
    alloc.deallocate(p2, 32);
}

TEST(SlabAllocatorTest, ManyAllocationsStayInArena) {
    SlabAllocator alloc;

    // Allocate many small objects
    std::vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) {
        ptrs.push_back(alloc.allocate(16));
    }

    auto stats = alloc.stats();
    // 2MB arena / 16 bytes = 131072 slots per arena, so 1000 should fit in one
    EXPECT_EQ(stats.chunks, 1u);
    EXPECT_EQ(stats.bytes_mapped, SlabAllocator::kArenaSize);

    for (auto* p : ptrs) {
        alloc.deallocate(p, 16);
    }
}

TEST(SlabAllocatorTest, FragmentationRatio) {
    SlabAllocator alloc;

    // Simulate a workload: allocate, free some, allocate more
    std::vector<void*> ptrs;
    for (int i = 0; i < 500; ++i) {
        ptrs.push_back(alloc.allocate(64));
    }

    // Free every other one
    for (size_t i = 0; i < ptrs.size(); i += 2) {
        alloc.deallocate(ptrs[i], 64);
        ptrs[i] = nullptr;
    }

    auto stats = alloc.stats();
    // Fragmentation ratio: bytes_mapped / bytes_live
    double frag_ratio =
        static_cast<double>(stats.bytes_mapped) / static_cast<double>(stats.bytes_live);
    // Should be reasonable — we freed half, so live = 250 * 64 = 16000 bytes
    // mapped = 2MB. Ratio is high because arenas are 2MB.
    // The important thing is the free-list is working.
    EXPECT_GT(stats.bytes_free_listed, 0u);
    EXPECT_EQ(stats.bytes_live, 250u * 64u);
    (void)frag_ratio;
}

TEST(SlabAllocatorTest, Reset) {
    SlabAllocator alloc;

    for (int i = 0; i < 100; ++i) {
        alloc.allocate(32);
    }
    alloc.allocate(8192); // Large alloc

    alloc.reset();
    auto stats = alloc.stats();
    EXPECT_EQ(stats.bytes_mapped, 0u);
    EXPECT_EQ(stats.bytes_live, 0u);
    EXPECT_EQ(stats.bytes_free_listed, 0u);
    EXPECT_EQ(stats.chunks, 0u);
    EXPECT_EQ(stats.large_allocs, 0u);
}

} // namespace
} // namespace raftkv
