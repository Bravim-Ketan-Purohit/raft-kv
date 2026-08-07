#pragma once

#include <optional>
#include <vector>

#include "raftkv/types.h"

namespace raftkv {

// In-memory Raft log. No I/O — persistence is handled by the WAL layer above.
class RaftLog {
public:
    RaftLog() = default;

    // Append a new entry. Returns its index.
    LogIndex append(Term term, Command cmd);

    // Get entry at index (1-based). Returns nullopt if out of range.
    std::optional<LogEntry> entry_at(LogIndex index) const;

    // Get term at index. Returns 0 if index is 0 or out of range.
    Term term_at(LogIndex index) const;

    // Last index in the log (0 if empty)
    LogIndex last_index() const;

    // Last term in the log (0 if empty)
    Term last_term() const;

    // Truncate all entries from `from_index` onwards (inclusive)
    void truncate_from(LogIndex from_index);

    // Get entries in range [lo, hi] (inclusive)
    std::vector<LogEntry> entries_in_range(LogIndex lo, LogIndex hi) const;

    // Get all entries from start_index onwards
    std::vector<LogEntry> entries_from(LogIndex start_index) const;

    // Number of entries
    size_t size() const { return entries_.size(); }

    // Check if log is at least as up-to-date as (last_term, last_index)
    bool is_up_to_date(Term other_last_term, LogIndex other_last_index) const;

    // Restore entries from persistent state (WAL replay)
    void restore(std::vector<LogEntry> entries);

private:
    // entries_[0] corresponds to log index 1
    std::vector<LogEntry> entries_;
};

} // namespace raftkv
