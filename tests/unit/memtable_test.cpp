#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "raftkv/memtable.h"

namespace raftkv {
namespace {

TEST(MemTableTest, PutAndGet) {
    MemTable mt;

    mt.put("key1", "value1");
    mt.put("key2", "value2");
    mt.put("key3", "value3");

    auto v1 = mt.get("key1");
    auto v2 = mt.get("key2");
    auto v3 = mt.get("key3");

    ASSERT_TRUE(v1.has_value());
    ASSERT_TRUE(v2.has_value());
    ASSERT_TRUE(v3.has_value());
    EXPECT_EQ(v1.value(), "value1");
    EXPECT_EQ(v2.value(), "value2");
    EXPECT_EQ(v3.value(), "value3");
}

TEST(MemTableTest, GetNonExistent) {
    MemTable mt;

    mt.put("exists", "yes");
    auto v = mt.get("does_not_exist");
    EXPECT_FALSE(v.has_value());
}

TEST(MemTableTest, UpdateExistingKey) {
    MemTable mt;

    mt.put("key", "version1");
    EXPECT_EQ(mt.get("key").value(), "version1");

    mt.put("key", "version2");
    EXPECT_EQ(mt.get("key").value(), "version2");

    mt.put("key", "version3");
    EXPECT_EQ(mt.get("key").value(), "version3");

    EXPECT_EQ(mt.size(), 1u);
}

TEST(MemTableTest, Delete) {
    MemTable mt;

    mt.put("key", "value");
    EXPECT_TRUE(mt.get("key").has_value());
    EXPECT_EQ(mt.size(), 1u);

    mt.remove("key");
    EXPECT_FALSE(mt.get("key").has_value());
    EXPECT_EQ(mt.size(), 0u);
}

TEST(MemTableTest, DeleteNonExistent) {
    MemTable mt;

    mt.remove("no_such_key"); // Should not crash
    EXPECT_EQ(mt.size(), 0u);
}

TEST(MemTableTest, PrefixScan) {
    MemTable mt;

    mt.put("user:1", "alice");
    mt.put("user:2", "bob");
    mt.put("user:3", "carol");
    mt.put("order:1", "pizza");
    mt.put("order:2", "pasta");

    auto users = mt.scan("user:", 100);
    EXPECT_EQ(users.size(), 3u);
    EXPECT_EQ(users[0].first, "user:1");
    EXPECT_EQ(users[0].second, "alice");

    auto orders = mt.scan("order:", 100);
    EXPECT_EQ(orders.size(), 2u);
}

TEST(MemTableTest, ScanWithLimit) {
    MemTable mt;

    for (int i = 0; i < 20; ++i) {
        mt.put("k" + std::to_string(i), "v" + std::to_string(i));
    }

    auto results = mt.scan("k", 5);
    EXPECT_EQ(results.size(), 5u);
}

TEST(MemTableTest, OrderedIteration) {
    MemTable mt;

    // Insert out of order
    mt.put("cherry", "3");
    mt.put("apple", "1");
    mt.put("banana", "2");

    auto all = mt.scan("", 100);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].first, "apple");
    EXPECT_EQ(all[1].first, "banana");
    EXPECT_EQ(all[2].first, "cherry");
}

TEST(MemTableTest, ConcurrentReaders) {
    MemTable mt;

    // Pre-populate
    for (int i = 0; i < 100; ++i) {
        mt.put("key" + std::to_string(i), "val" + std::to_string(i));
    }

    // Launch readers concurrently
    std::vector<std::thread> readers;
    std::atomic<int> errors{0};

    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&mt, &errors]() {
            for (int i = 0; i < 100; ++i) {
                auto v = mt.get("key" + std::to_string(i));
                if (!v.has_value()) {
                    errors.fetch_add(1);
                }
            }
        });
    }

    for (auto& thread : readers) {
        thread.join();
    }

    EXPECT_EQ(errors.load(), 0);
}

TEST(MemTableTest, AllocatorStats) {
    MemTable mt;

    for (int i = 0; i < 100; ++i) {
        mt.put("key" + std::to_string(i), "value" + std::to_string(i));
    }

    auto stats = mt.alloc_stats();
    EXPECT_GT(stats.bytes_mapped, 0u);
    EXPECT_GT(stats.bytes_live, 0u);
    EXPECT_GT(stats.chunks, 0u);
}

TEST(MemTableTest, EpochBasedReclamation) {
    MemTable mt;

    // Insert and overwrite to generate retired nodes
    for (int i = 0; i < 50; ++i) {
        mt.put("key", "value_" + std::to_string(i));
    }

    // The current value should always be accessible
    auto v = mt.get("key");
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v.value(), "value_49");
}

} // namespace
} // namespace raftkv
