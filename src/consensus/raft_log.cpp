#include "raftkv/raft_log.h"

#include <algorithm>

namespace raftkv {

LogIndex RaftLog::append(Term term, Command cmd) {
    LogIndex index = entries_.size() + 1;
    entries_.push_back(LogEntry{index, term, std::move(cmd)});
    return index;
}

std::optional<LogEntry> RaftLog::entry_at(LogIndex index) const {
    if (index == 0 || index > entries_.size()) {
        return std::nullopt;
    }
    return entries_[index - 1];
}

Term RaftLog::term_at(LogIndex index) const {
    if (index == 0 || index > entries_.size()) {
        return 0;
    }
    return entries_[index - 1].term;
}

LogIndex RaftLog::last_index() const {
    return entries_.size();
}

Term RaftLog::last_term() const {
    if (entries_.empty()) return 0;
    return entries_.back().term;
}

void RaftLog::truncate_from(LogIndex from_index) {
    if (from_index <= entries_.size()) {
        entries_.resize(from_index - 1);
    }
}

std::vector<LogEntry> RaftLog::entries_in_range(LogIndex lo, LogIndex hi) const {
    std::vector<LogEntry> result;
    if (lo == 0) lo = 1;
    if (hi > entries_.size()) hi = entries_.size();
    for (LogIndex i = lo; i <= hi; ++i) {
        result.push_back(entries_[i - 1]);
    }
    return result;
}

std::vector<LogEntry> RaftLog::entries_from(LogIndex start_index) const {
    return entries_in_range(start_index, entries_.size());
}

bool RaftLog::is_up_to_date(Term other_last_term, LogIndex other_last_index) const {
    Term my_last_term = last_term();
    if (my_last_term != other_last_term) {
        return my_last_term >= other_last_term;
    }
    return last_index() >= other_last_index;
}

void RaftLog::restore(std::vector<LogEntry> entries) {
    entries_ = std::move(entries);
}

} // namespace raftkv
