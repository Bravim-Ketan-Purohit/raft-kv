#include "raftkv/slab_allocator.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <new>

namespace raftkv {

SlabAllocator::SlabAllocator() {
    free_lists_.fill(nullptr);
}

SlabAllocator::~SlabAllocator() {
    // Free large allocations
    for (auto& alloc : large_allocs_) {
        ::operator delete(alloc.ptr);
    }
    // Arenas are cleaned up by unique_ptr
}

size_t SlabAllocator::size_class_index(size_t size) {
    // Find the smallest size class that fits
    for (size_t i = 0; i < kNumSizeClasses; ++i) {
        if (size <= kSizeClasses[i]) {
            return i;
        }
    }
    return kNumSizeClasses; // Too large for slab
}

void SlabAllocator::refill_free_list(size_t class_idx) {
    size_t slab_size = kSizeClasses[class_idx];

    // Allocate a new arena
    Arena arena;
    arena.data = std::make_unique<uint8_t[]>(kArenaSize);
    arena.used = 0;

    // Carve slabs from the arena
    size_t count = kArenaSize / slab_size;
    uint8_t* base = arena.data.get();

    for (size_t i = 0; i < count; ++i) {
        auto* node = reinterpret_cast<FreeNode*>(base + i * slab_size);
        node->next = free_lists_[class_idx];
        free_lists_[class_idx] = node;
    }

    bytes_free_listed_ += count * slab_size;
    arena.used = count * slab_size;
    arenas_.push_back(std::move(arena));
}

void* SlabAllocator::allocate(size_t size) {
    bytes_requested_ += size;

    size_t idx = size_class_index(size);
    if (idx == kNumSizeClasses) {
        // Large allocation: fall through to operator new
        void* ptr = ::operator new(size);
        large_allocs_.push_back({ptr, size});
        bytes_live_ += size;
        return ptr;
    }

    // Check free list
    if (free_lists_[idx] == nullptr) {
        refill_free_list(idx);
    }

    FreeNode* node = free_lists_[idx];
    free_lists_[idx] = node->next;

    size_t actual_size = kSizeClasses[idx];
    bytes_free_listed_ -= actual_size;
    bytes_live_ += actual_size;

    return static_cast<void*>(node);
}

void SlabAllocator::deallocate(void* ptr, size_t size) {
    size_t idx = size_class_index(size);
    if (idx == kNumSizeClasses) {
        // Large allocation
        auto it = std::find_if(large_allocs_.begin(), large_allocs_.end(),
                               [ptr](const LargeAlloc& a) { return a.ptr == ptr; });
        if (it != large_allocs_.end()) {
            bytes_live_ -= it->size;
            ::operator delete(ptr);
            large_allocs_.erase(it);
        }
        return;
    }

    size_t actual_size = kSizeClasses[idx];
    auto* node = static_cast<FreeNode*>(ptr);
    node->next = free_lists_[idx];
    free_lists_[idx] = node;

    bytes_live_ -= actual_size;
    bytes_free_listed_ += actual_size;
}

AllocStats SlabAllocator::stats() const {
    AllocStats s;
    s.bytes_requested = bytes_requested_;
    s.bytes_mapped = arenas_.size() * kArenaSize;
    s.bytes_live = bytes_live_;
    s.bytes_free_listed = bytes_free_listed_;
    s.chunks = arenas_.size();
    s.large_allocs = large_allocs_.size();
    return s;
}

void SlabAllocator::reset() {
    free_lists_.fill(nullptr);
    for (auto& alloc : large_allocs_) {
        ::operator delete(alloc.ptr);
    }
    large_allocs_.clear();
    arenas_.clear();
    bytes_requested_ = 0;
    bytes_live_ = 0;
    bytes_free_listed_ = 0;
}

} // namespace raftkv
