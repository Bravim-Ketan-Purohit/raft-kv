#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace raftkv {

// Allocation statistics for fragmentation measurement
struct AllocStats {
    size_t bytes_requested = 0;   // Total bytes requested by callers
    size_t bytes_mapped = 0;      // Total bytes mapped from OS (arena chunks)
    size_t bytes_live = 0;        // Bytes currently in use
    size_t bytes_free_listed = 0; // Bytes on free lists
    size_t chunks = 0;            // Number of arena chunks allocated
    size_t large_allocs = 0;      // Allocations that bypassed the slab (> 2048)
};

// Slab allocator with size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes.
// Carved from 2 MB arena chunks. Single-writer, no locks needed.
// Allocations over 2048 bytes fall through to ::operator new.
class SlabAllocator {
public:
    static constexpr size_t kArenaSize = 2 * 1024 * 1024; // 2 MB
    static constexpr size_t kNumSizeClasses = 8;
    static constexpr std::array<size_t, kNumSizeClasses> kSizeClasses = {
        16, 32, 64, 128, 256, 512, 1024, 2048};
    static constexpr size_t kMaxSlabSize = 2048;

    SlabAllocator();
    ~SlabAllocator();

    // Non-copyable, non-movable
    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    // Allocate memory of at least `size` bytes
    void* allocate(size_t size);

    // Deallocate memory previously allocated with allocate()
    void deallocate(void* ptr, size_t size);

    // Get allocation statistics
    AllocStats stats() const;

    // Reset all allocations (invalidates all pointers)
    void reset();

private:
    struct FreeNode {
        FreeNode* next;
    };

    struct Arena {
        std::unique_ptr<uint8_t[]> data;
        size_t used = 0;
    };

    // Returns size class index for a given size, or kNumSizeClasses if too large
    static size_t size_class_index(size_t size);

    // Allocate a new arena chunk and carve slabs from it for the given size class
    void refill_free_list(size_t class_idx);

    std::array<FreeNode*, kNumSizeClasses> free_lists_;
    std::vector<Arena> arenas_;

    // Track large allocations (> kMaxSlabSize)
    struct LargeAlloc {
        void* ptr;
        size_t size;
    };
    std::vector<LargeAlloc> large_allocs_;

    // Statistics
    size_t bytes_requested_ = 0;
    size_t bytes_live_ = 0;
    size_t bytes_free_listed_ = 0;
};

} // namespace raftkv
