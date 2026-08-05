#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace raftkv {

using NodeId = uint32_t;
using Term = uint64_t;
using LogIndex = uint64_t;
using Millis = uint64_t;

// Result type for operations that can fail
template <typename T>
struct Result {
    T value;
    bool ok;
    std::string error;

    static Result success(T val) { return {std::move(val), true, ""}; }
    static Result failure(std::string err) { return {T{}, false, std::move(err)}; }
};

template <>
struct Result<void> {
    bool ok;
    std::string error;

    static Result success() { return {true, ""}; }
    static Result failure(std::string err) { return {false, std::move(err)}; }
};

// Log entry command types
struct PutCommand {
    std::string key;
    std::string value;
};

struct DeleteCommand {
    std::string key;
};

struct NoOpCommand {};

using Command = std::variant<PutCommand, DeleteCommand, NoOpCommand>;

// A single Raft log entry
struct LogEntry {
    LogIndex index;
    Term term;
    Command command;
};

// Read consistency modes
enum class ReadMode { LINEARIZABLE, STALE_OK };

// Node roles
enum class Role { FOLLOWER, CANDIDATE, LEADER };

inline const char* role_to_string(Role r) {
    switch (r) {
        case Role::FOLLOWER: return "follower";
        case Role::CANDIDATE: return "candidate";
        case Role::LEADER: return "leader";
    }
    return "unknown";
}

} // namespace raftkv
