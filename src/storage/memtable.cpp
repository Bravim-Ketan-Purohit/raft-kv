#include "raftkv/memtable.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

namespace raftkv {

// --- Node creation ---

MemTable::Node* MemTable::Node::create(SlabAllocator& alloc, std::string_view key,
                                        std::string_view value, int height, bool deleted) {
    // Calculate total size: Node base + height * atomic<Node*>
    size_t node_size = sizeof(Node) + height * sizeof(std::atomic<Node*>);
    void* mem = alloc.allocate(node_size);
    Node* node = new (mem) Node();
    node->key = std::string(key);
    node->value = std::string(value);
    node->deleted = deleted;
    node->height = height;

    // Initialize next pointers to nullptr
    for (int i = 0; i < height; ++i) {
        new (&node->next[i]) std::atomic<Node*>(nullptr);
    }

    return node;
}

// --- EpochGuard ---

MemTable::EpochGuard::EpochGuard(const MemTable& table) : table_(table) {
    slot_ = table_.next_slot_.fetch_add(1, std::memory_order_relaxed) % kMaxReaders;
    uint64_t epoch = table_.global_epoch_.load(std::memory_order_acquire);
    table_.reader_epochs_[slot_].store(epoch, std::memory_order_release);
}

MemTable::EpochGuard::~EpochGuard() {
    // Mark slot as inactive (UINT64_MAX means not reading)
    table_.reader_epochs_[slot_].store(std::numeric_limits<uint64_t>::max(),
                                       std::memory_order_release);
}

// --- MemTable ---

MemTable::MemTable() {
    // Initialize reader epochs to inactive
    for (size_t i = 0; i < kMaxReaders; ++i) {
        reader_epochs_[i].store(std::numeric_limits<uint64_t>::max(), std::memory_order_relaxed);
    }

    // Create sentinel head node with max height
    head_ = Node::create(allocator_, "", "", kMaxHeight, false);
}

MemTable::~MemTable() {
    // Walk through and let allocator handle everything at reset
    // Nodes allocated via SlabAllocator, which cleans up arenas
}

int MemTable::random_height() {
    int height = 1;
    // p = 1/4 branching probability
    while (height < kMaxHeight && (rng_() % 4) == 0) {
        ++height;
    }
    return height;
}

MemTable::Node* MemTable::find_greater_or_equal(std::string_view key, Node* preds[]) const {
    Node* curr = head_;
    for (int level = kMaxHeight - 1; level >= 0; --level) {
        Node* next = curr->next[level].load(std::memory_order_acquire);
        while (next != nullptr && next->key < key) {
            curr = next;
            next = curr->next[level].load(std::memory_order_acquire);
        }
        if (preds != nullptr) {
            preds[level] = curr;
        }
    }
    return curr->next[0].load(std::memory_order_acquire);
}

void MemTable::put(std::string_view key, std::string_view value) {
    Node* preds[kMaxHeight];
    Node* found = find_greater_or_equal(key, preds);

    // If key exists, update in-place (single writer)
    if (found != nullptr && found->key == key) {
        // Create new node with the updated value and link it in
        int height = found->height;
        Node* new_node = Node::create(allocator_, key, value, height, false);

        // Link the new node's next pointers
        for (int i = 0; i < height; ++i) {
            new_node->next[i].store(found->next[i].load(std::memory_order_relaxed),
                                    std::memory_order_relaxed);
        }

        // Atomically swap in the new node at each level (release for readers)
        for (int i = 0; i < height; ++i) {
            preds[i]->next[i].store(new_node, std::memory_order_release);
        }

        // If was a tombstone, count is increasing
        if (found->deleted) {
            size_.fetch_add(1, std::memory_order_relaxed);
        }

        // Retire the old node
        retire_node(found);
        return;
    }

    // Insert new node
    int height = random_height();
    Node* new_node = Node::create(allocator_, key, value, height, false);

    // Re-find preds if our height is higher than what we initially searched
    // (preds from find_greater_or_equal covers all levels since head is max height)

    for (int i = 0; i < height; ++i) {
        new_node->next[i].store(preds[i]->next[i].load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
    }

    // Publish bottom-up. Level 0 last guarantees visibility.
    for (int i = height - 1; i >= 0; --i) {
        preds[i]->next[i].store(new_node, std::memory_order_release);
    }

    size_.fetch_add(1, std::memory_order_relaxed);

    // Advance epoch and try to reclaim
    global_epoch_.fetch_add(1, std::memory_order_release);
    try_reclaim();
}

void MemTable::remove(std::string_view key) {
    Node* preds[kMaxHeight];
    Node* found = find_greater_or_equal(key, preds);

    if (found == nullptr || found->key != key || found->deleted) {
        return; // Key not found or already deleted
    }

    // Create a tombstone node
    int height = found->height;
    Node* tombstone = Node::create(allocator_, key, "", height, true);

    // Link tombstone's next pointers
    for (int i = 0; i < height; ++i) {
        tombstone->next[i].store(found->next[i].load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
    }

    // Swap in the tombstone
    for (int i = 0; i < height; ++i) {
        preds[i]->next[i].store(tombstone, std::memory_order_release);
    }

    size_.fetch_sub(1, std::memory_order_relaxed);
    retire_node(found);

    global_epoch_.fetch_add(1, std::memory_order_release);
    try_reclaim();
}

std::optional<std::string> MemTable::get(std::string_view key) const {
    EpochGuard guard(*this);

    Node* node = find_greater_or_equal(key, nullptr);
    if (node != nullptr && node->key == key && !node->deleted) {
        return node->value;
    }
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> MemTable::scan(std::string_view prefix,
                                                                 size_t max_results) const {
    EpochGuard guard(*this);

    std::vector<std::pair<std::string, std::string>> results;
    Node* node = find_greater_or_equal(prefix, nullptr);

    while (node != nullptr && results.size() < max_results) {
        if (node->key.substr(0, prefix.size()) != prefix) {
            break; // Past the prefix range
        }
        if (!node->deleted) {
            results.emplace_back(node->key, node->value);
        }
        node = node->next[0].load(std::memory_order_acquire);
    }

    return results;
}

void MemTable::retire_node(Node* node) {
    uint64_t epoch = global_epoch_.load(std::memory_order_relaxed);
    retired_nodes_.push_back({node, epoch});
}

uint64_t MemTable::min_reader_epoch() const {
    uint64_t min_epoch = std::numeric_limits<uint64_t>::max();
    for (size_t i = 0; i < kMaxReaders; ++i) {
        uint64_t e = reader_epochs_[i].load(std::memory_order_acquire);
        if (e < min_epoch) {
            min_epoch = e;
        }
    }
    return min_epoch;
}

void MemTable::try_reclaim() {
    if (retired_nodes_.empty()) return;

    uint64_t safe_epoch = min_reader_epoch();
    if (safe_epoch == std::numeric_limits<uint64_t>::max()) {
        // No active readers — we can reclaim everything
        safe_epoch = global_epoch_.load(std::memory_order_relaxed);
    }

    auto it = std::remove_if(retired_nodes_.begin(), retired_nodes_.end(),
                             [&](const RetiredNode& rn) {
                                 if (rn.retire_epoch < safe_epoch) {
                                     // Safe to free
                                     size_t node_size =
                                         sizeof(Node) + rn.node->height * sizeof(std::atomic<Node*>);
                                     rn.node->~Node();
                                     allocator_.deallocate(rn.node, node_size);
                                     return true;
                                 }
                                 return false;
                             });
    retired_nodes_.erase(it, retired_nodes_.end());
}

} // namespace raftkv
