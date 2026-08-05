#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "raftkv/slab_allocator.h"

namespace raftkv {

// Lock-free skiplist-based memtable.
// Single writer (the apply thread) publishes with release; readers acquire.
// Epoch-based reclamation for safe memory deallocation.
class MemTable {
public:
    static constexpr int kMaxHeight = 20;

    MemTable();
    ~MemTable();

    // Non-copyable
    MemTable(const MemTable&) = delete;
    MemTable& operator=(const MemTable&) = delete;

    // Writer interface (single-writer, no concurrency needed)
    void put(std::string_view key, std::string_view value);
    void remove(std::string_view key);

    // Reader interface (lock-free, concurrent with writer)
    std::optional<std::string> get(std::string_view key) const;

    // Prefix scan (lock-free reader)
    std::vector<std::pair<std::string, std::string>> scan(std::string_view prefix,
                                                          size_t max_results = 100) const;

    // Number of live entries
    size_t size() const { return size_.load(std::memory_order_relaxed); }

    // Epoch-based reclamation: readers must enter/exit epochs
    struct EpochGuard {
        EpochGuard(const MemTable& table);
        ~EpochGuard();
        EpochGuard(const EpochGuard&) = delete;
        EpochGuard& operator=(const EpochGuard&) = delete;

    private:
        const MemTable& table_;
        size_t slot_;
    };

    // Get allocator stats
    AllocStats alloc_stats() const { return allocator_.stats(); }

private:
    struct Node {
        std::string key;
        std::string value;
        bool deleted; // Tombstone marker
        int height;
        std::atomic<Node*> next[]; // Flexible array member

        static Node* create(SlabAllocator& alloc, std::string_view key, std::string_view value,
                            int height, bool deleted = false);
    };

    // Epoch-based reclamation internals
    static constexpr size_t kMaxReaders = 64;
    mutable std::atomic<uint64_t> global_epoch_{0};
    mutable std::atomic<uint64_t> reader_epochs_[kMaxReaders];
    mutable std::atomic<size_t> next_slot_{0};

    struct RetiredNode {
        Node* node;
        uint64_t retire_epoch;
    };
    std::vector<RetiredNode> retired_nodes_;

    void retire_node(Node* node);
    void try_reclaim();
    uint64_t min_reader_epoch() const;

    // Skiplist internals
    Node* head_;
    std::atomic<size_t> size_{0};
    mutable SlabAllocator allocator_;
    std::mt19937 rng_{42}; // Writer-only, deterministic seed

    int random_height();
    Node* find_greater_or_equal(std::string_view key, Node* preds[]) const;
};

} // namespace raftkv
