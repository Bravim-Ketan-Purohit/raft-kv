#include <gtest/gtest.h>

#include "raftkv/raft_log.h"

namespace raftkv {
namespace {

TEST(RaftLogTest, EmptyLog) {
    RaftLog log;
    EXPECT_EQ(log.last_index(), 0u);
    EXPECT_EQ(log.last_term(), 0u);
    EXPECT_EQ(log.size(), 0u);
    EXPECT_FALSE(log.entry_at(1).has_value());
}

TEST(RaftLogTest, AppendAndRetrieve) {
    RaftLog log;

    auto idx1 = log.append(1, PutCommand{"key1", "val1"});
    auto idx2 = log.append(1, PutCommand{"key2", "val2"});
    auto idx3 = log.append(2, DeleteCommand{"key1"});

    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(idx2, 2u);
    EXPECT_EQ(idx3, 3u);

    auto e1 = log.entry_at(1);
    ASSERT_TRUE(e1.has_value());
    EXPECT_EQ(e1->index, 1u);
    EXPECT_EQ(e1->term, 1u);
    EXPECT_TRUE(std::holds_alternative<PutCommand>(e1->command));

    EXPECT_EQ(log.last_index(), 3u);
    EXPECT_EQ(log.last_term(), 2u);
}

TEST(RaftLogTest, TermAt) {
    RaftLog log;

    log.append(1, NoOpCommand{});
    log.append(1, NoOpCommand{});
    log.append(2, NoOpCommand{});
    log.append(3, NoOpCommand{});

    EXPECT_EQ(log.term_at(0), 0u); // Before log
    EXPECT_EQ(log.term_at(1), 1u);
    EXPECT_EQ(log.term_at(2), 1u);
    EXPECT_EQ(log.term_at(3), 2u);
    EXPECT_EQ(log.term_at(4), 3u);
    EXPECT_EQ(log.term_at(5), 0u); // Beyond log
}

TEST(RaftLogTest, TruncateFrom) {
    RaftLog log;

    log.append(1, NoOpCommand{});
    log.append(1, NoOpCommand{});
    log.append(2, NoOpCommand{});
    log.append(2, NoOpCommand{});

    EXPECT_EQ(log.size(), 4u);

    log.truncate_from(3);
    EXPECT_EQ(log.size(), 2u);
    EXPECT_EQ(log.last_index(), 2u);
    EXPECT_EQ(log.last_term(), 1u);
}

TEST(RaftLogTest, EntriesInRange) {
    RaftLog log;

    log.append(1, PutCommand{"a", "1"});
    log.append(1, PutCommand{"b", "2"});
    log.append(2, PutCommand{"c", "3"});
    log.append(2, PutCommand{"d", "4"});

    auto entries = log.entries_in_range(2, 3);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].index, 2u);
    EXPECT_EQ(entries[1].index, 3u);
}

TEST(RaftLogTest, EntriesFrom) {
    RaftLog log;

    log.append(1, NoOpCommand{});
    log.append(2, NoOpCommand{});
    log.append(3, NoOpCommand{});

    auto entries = log.entries_from(2);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].index, 2u);
    EXPECT_EQ(entries[1].index, 3u);
}

TEST(RaftLogTest, IsUpToDate) {
    RaftLog log;

    // Empty log is up-to-date compared to empty
    EXPECT_TRUE(log.is_up_to_date(0, 0));

    log.append(1, NoOpCommand{});
    log.append(2, NoOpCommand{});

    // Our log: term 2, index 2
    EXPECT_TRUE(log.is_up_to_date(1, 5));  // Higher term wins
    EXPECT_TRUE(log.is_up_to_date(2, 2));  // Equal
    EXPECT_TRUE(log.is_up_to_date(2, 1));  // Same term, shorter log
    EXPECT_FALSE(log.is_up_to_date(2, 3)); // Same term, longer log
    EXPECT_FALSE(log.is_up_to_date(3, 1)); // Higher term
}

TEST(RaftLogTest, Restore) {
    RaftLog log;

    std::vector<LogEntry> entries = {
        {1, 1, PutCommand{"a", "1"}},
        {2, 1, PutCommand{"b", "2"}},
        {3, 2, NoOpCommand{}},
    };

    log.restore(entries);
    EXPECT_EQ(log.size(), 3u);
    EXPECT_EQ(log.last_index(), 3u);
    EXPECT_EQ(log.last_term(), 2u);
    EXPECT_EQ(log.term_at(1), 1u);
    EXPECT_EQ(log.term_at(3), 2u);
}

} // namespace
} // namespace raftkv
