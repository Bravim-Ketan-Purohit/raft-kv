#include <gtest/gtest.h>

#include <filesystem>

#include "raftkv/wal.h"

namespace raftkv {
namespace {

class WalTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "raftkv_wal_test";
        std::filesystem::create_directories(test_dir_);
        wal_path_ = test_dir_ / "test.wal";
    }

    void TearDown() override { std::filesystem::remove_all(test_dir_); }

    std::filesystem::path test_dir_;
    std::filesystem::path wal_path_;
};

TEST_F(WalTest, AppendAndReplay) {
    {
        Wal wal(wal_path_, true); // no-fsync for test speed

        LogEntry e1{1, 1, PutCommand{"key1", "value1"}};
        LogEntry e2{2, 1, PutCommand{"key2", "value2"}};
        LogEntry e3{3, 2, DeleteCommand{"key1"}};

        EXPECT_TRUE(wal.append(e1).ok);
        EXPECT_TRUE(wal.append(e2).ok);
        EXPECT_TRUE(wal.append(e3).ok);
    }

    auto result = Wal::replay(wal_path_);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.value.entries.size(), 3u);
    EXPECT_FALSE(result.value.truncated_tail);

    EXPECT_EQ(result.value.entries[0].index, 1u);
    EXPECT_EQ(result.value.entries[0].term, 1u);
    EXPECT_TRUE(std::holds_alternative<PutCommand>(result.value.entries[0].command));

    auto& put_cmd = std::get<PutCommand>(result.value.entries[0].command);
    EXPECT_EQ(put_cmd.key, "key1");
    EXPECT_EQ(put_cmd.value, "value1");

    EXPECT_TRUE(std::holds_alternative<DeleteCommand>(result.value.entries[2].command));
}

TEST_F(WalTest, MetadataPersistence) {
    {
        Wal wal(wal_path_, true);
        EXPECT_TRUE(wal.write_metadata(5, NodeId{3}).ok);
    }

    auto result = Wal::replay(wal_path_);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.value.term, 5u);
    ASSERT_TRUE(result.value.voted_for.has_value());
    EXPECT_EQ(result.value.voted_for.value(), 3u);
}

TEST_F(WalTest, TornTailTruncation) {
    // Write valid entries then append garbage (simulating crash mid-write)
    {
        Wal wal(wal_path_, true);
        LogEntry e1{1, 1, PutCommand{"key1", "val1"}};
        LogEntry e2{2, 1, PutCommand{"key2", "val2"}};
        EXPECT_TRUE(wal.append(e1).ok);
        EXPECT_TRUE(wal.append(e2).ok);
    }

    // Append garbage to simulate torn write
    {
        std::ofstream f(wal_path_, std::ios::binary | std::ios::app);
        uint8_t garbage[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03};
        f.write(reinterpret_cast<const char*>(garbage), sizeof(garbage));
    }

    auto result = Wal::replay(wal_path_);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.value.entries.size(), 2u); // Only valid entries survive
    EXPECT_TRUE(result.value.truncated_tail);
}

TEST_F(WalTest, EmptyFileReplay) {
    // Create empty file
    { std::ofstream f(wal_path_); }

    auto result = Wal::replay(wal_path_);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.value.entries.size(), 0u);
    EXPECT_FALSE(result.value.truncated_tail);
}

TEST_F(WalTest, NonExistentFileReplay) {
    auto result = Wal::replay(test_dir_ / "nonexistent.wal");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.value.entries.size(), 0u);
}

TEST_F(WalTest, NoOpCommand) {
    {
        Wal wal(wal_path_, true);
        LogEntry e{1, 1, NoOpCommand{}};
        EXPECT_TRUE(wal.append(e).ok);
    }

    auto result = Wal::replay(wal_path_);
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.value.entries.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<NoOpCommand>(result.value.entries[0].command));
}

TEST_F(WalTest, ManyEntries) {
    {
        Wal wal(wal_path_, true);
        for (int i = 0; i < 1000; ++i) {
            LogEntry e{static_cast<LogIndex>(i + 1), 1,
                       PutCommand{"key" + std::to_string(i), "val" + std::to_string(i)}};
            EXPECT_TRUE(wal.append(e).ok);
        }
    }

    auto result = Wal::replay(wal_path_);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.value.entries.size(), 1000u);
}

} // namespace
} // namespace raftkv
