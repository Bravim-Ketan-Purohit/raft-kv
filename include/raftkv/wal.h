#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "raftkv/types.h"

namespace raftkv {

// Write-Ahead Log. Record format: [u32 len][u32 crc32][payload]
// Written and fsync'd before the node acknowledges any RPC.
// On startup: replay, truncate torn tail (bad CRC or short read).
class Wal {
public:
    explicit Wal(const std::filesystem::path& path, bool no_fsync = false);
    ~Wal();

    // Non-copyable
    Wal(const Wal&) = delete;
    Wal& operator=(const Wal&) = delete;

    // Append a log entry. Writes + fsyncs before returning.
    Result<void> append(const LogEntry& entry);

    // Append persistent metadata (term, votedFor). Writes + fsyncs.
    Result<void> write_metadata(Term term, std::optional<NodeId> voted_for);

    // Replay the WAL from disk. Returns entries and metadata.
    struct ReplayResult {
        std::vector<LogEntry> entries;
        Term term = 0;
        std::optional<NodeId> voted_for;
        bool truncated_tail = false; // True if we had to truncate a torn record
    };
    static Result<ReplayResult> replay(const std::filesystem::path& path);

    // Sync to disk
    void sync();

    // Get the WAL file path
    const std::filesystem::path& path() const { return path_; }

private:
    // Record types
    enum class RecordType : uint8_t { LOG_ENTRY = 1, METADATA = 2 };

    // Serialize/deserialize
    static std::vector<uint8_t> serialize_entry(const LogEntry& entry);
    static LogEntry deserialize_entry(const uint8_t* data, size_t len);
    static std::vector<uint8_t> serialize_metadata(Term term, std::optional<NodeId> voted_for);

    // CRC32
    static uint32_t compute_crc32(const uint8_t* data, size_t len);

    std::filesystem::path path_;
    std::ofstream file_;
    bool no_fsync_;
};

} // namespace raftkv
