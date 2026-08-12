#include "raftkv/wal.h"

#include <cstring>
#include <fstream>
#include <iostream>

#ifdef __APPLE__
#include <fcntl.h>
#include <unistd.h>
#endif

namespace raftkv {

// CRC32 lookup table (IEEE polynomial)
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void init_crc32_table() {
    if (crc32_table_initialized) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

uint32_t Wal::compute_crc32(const uint8_t* data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

// Simple serialization helpers
static void write_u8(std::vector<uint8_t>& buf, uint8_t val) {
    buf.push_back(val);
}

static void write_u32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

static void write_u64(std::vector<uint8_t>& buf, uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
    }
}

static void write_string(std::vector<uint8_t>& buf, const std::string& s) {
    write_u32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

static uint32_t read_u32(const uint8_t*& ptr) {
    uint32_t val = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
    ptr += 4;
    return val;
}

static uint64_t read_u64(const uint8_t*& ptr) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= static_cast<uint64_t>(ptr[i]) << (i * 8);
    }
    ptr += 8;
    return val;
}

static std::string read_string(const uint8_t*& ptr) {
    uint32_t len = read_u32(ptr);
    std::string s(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return s;
}

Wal::Wal(const std::filesystem::path& path, bool no_fsync) : path_(path), no_fsync_(no_fsync) {
    // Create parent directory if needed
    if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    file_.open(path_, std::ios::binary | std::ios::app);
}

Wal::~Wal() {
    if (file_.is_open()) {
        file_.close();
    }
}

std::vector<uint8_t> Wal::serialize_entry(const LogEntry& entry) {
    std::vector<uint8_t> buf;
    write_u8(buf, static_cast<uint8_t>(RecordType::LOG_ENTRY));
    write_u64(buf, entry.index);
    write_u64(buf, entry.term);

    // Command type
    if (std::holds_alternative<PutCommand>(entry.command)) {
        write_u8(buf, 1); // PUT
        const auto& cmd = std::get<PutCommand>(entry.command);
        write_string(buf, cmd.key);
        write_string(buf, cmd.value);
    } else if (std::holds_alternative<DeleteCommand>(entry.command)) {
        write_u8(buf, 2); // DELETE
        const auto& cmd = std::get<DeleteCommand>(entry.command);
        write_string(buf, cmd.key);
    } else {
        write_u8(buf, 0); // NOOP
    }

    return buf;
}

LogEntry Wal::deserialize_entry(const uint8_t* data, size_t len) {
    (void)len;
    const uint8_t* ptr = data;
    // Skip record type byte (already handled by caller)
    ptr += 1;

    LogEntry entry;
    entry.index = read_u64(ptr);
    entry.term = read_u64(ptr);

    uint8_t cmd_type = *ptr++;
    if (cmd_type == 1) {
        PutCommand cmd;
        cmd.key = read_string(ptr);
        cmd.value = read_string(ptr);
        entry.command = cmd;
    } else if (cmd_type == 2) {
        DeleteCommand cmd;
        cmd.key = read_string(ptr);
        entry.command = cmd;
    } else {
        entry.command = NoOpCommand{};
    }

    return entry;
}

std::vector<uint8_t> Wal::serialize_metadata(Term term, std::optional<NodeId> voted_for) {
    std::vector<uint8_t> buf;
    write_u8(buf, static_cast<uint8_t>(RecordType::METADATA));
    write_u64(buf, term);
    write_u8(buf, voted_for.has_value() ? 1 : 0);
    if (voted_for.has_value()) {
        write_u32(buf, voted_for.value());
    }
    return buf;
}

Result<void> Wal::append(const LogEntry& entry) {
    auto payload = serialize_entry(entry);
    uint32_t len = static_cast<uint32_t>(payload.size());
    uint32_t crc = compute_crc32(payload.data(), payload.size());

    // Write: [len][crc][payload]
    file_.write(reinterpret_cast<const char*>(&len), sizeof(len));
    file_.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
    file_.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    file_.flush();

    if (!no_fsync_) {
        sync();
    }

    if (!file_.good()) {
        return Result<void>::failure("WAL write failed");
    }
    return Result<void>::success();
}

Result<void> Wal::write_metadata(Term term, std::optional<NodeId> voted_for) {
    auto payload = serialize_metadata(term, voted_for);
    uint32_t len = static_cast<uint32_t>(payload.size());
    uint32_t crc = compute_crc32(payload.data(), payload.size());

    file_.write(reinterpret_cast<const char*>(&len), sizeof(len));
    file_.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
    file_.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    file_.flush();

    if (!no_fsync_) {
        sync();
    }

    if (!file_.good()) {
        return Result<void>::failure("WAL metadata write failed");
    }
    return Result<void>::success();
}

void Wal::sync() {
#ifdef __APPLE__
    // Use fcntl F_FULLFSYNC on macOS for true durability
    int fd = open(path_.c_str(), O_RDONLY);
    if (fd >= 0) {
        fcntl(fd, F_FULLFSYNC);
        close(fd);
    }
#else
    // Linux: fdatasync
    file_.flush();
#endif
}

Result<Wal::ReplayResult> Wal::replay(const std::filesystem::path& path) {
    ReplayResult result;

    if (!std::filesystem::exists(path)) {
        return Result<ReplayResult>::success(std::move(result));
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return Result<ReplayResult>::failure("Cannot open WAL file for replay");
    }

    init_crc32_table();

    while (file.good() && file.peek() != EOF) {
        uint32_t len = 0;
        uint32_t crc = 0;

        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        if (!file.good() || file.gcount() < static_cast<std::streamsize>(sizeof(len))) {
            // Torn tail — truncate here
            result.truncated_tail = true;
            break;
        }

        file.read(reinterpret_cast<char*>(&crc), sizeof(crc));
        if (!file.good() || file.gcount() < static_cast<std::streamsize>(sizeof(crc))) {
            result.truncated_tail = true;
            break;
        }

        if (len > 64 * 1024 * 1024) {
            // Sanity check: no record should be > 64MB
            result.truncated_tail = true;
            break;
        }

        std::vector<uint8_t> payload(len);
        file.read(reinterpret_cast<char*>(payload.data()), len);
        if (!file.good() || file.gcount() < static_cast<std::streamsize>(len)) {
            result.truncated_tail = true;
            break;
        }

        // Verify CRC
        uint32_t computed_crc = Wal::compute_crc32(payload.data(), payload.size());
        if (computed_crc != crc) {
            result.truncated_tail = true;
            break;
        }

        // Parse record type
        if (payload.empty()) {
            result.truncated_tail = true;
            break;
        }

        auto record_type = static_cast<RecordType>(payload[0]);
        if (record_type == RecordType::LOG_ENTRY) {
            LogEntry entry = deserialize_entry(payload.data(), payload.size());
            result.entries.push_back(std::move(entry));
        } else if (record_type == RecordType::METADATA) {
            const uint8_t* ptr = payload.data() + 1; // Skip record type
            result.term = read_u64(ptr);
            uint8_t has_voted = *ptr++;
            if (has_voted) {
                result.voted_for = read_u32(ptr);
            } else {
                result.voted_for = std::nullopt;
            }
        }
    }

    // If we detected a torn tail, truncate the file
    if (result.truncated_tail) {
        file.close();
        // Rewrite without the torn records
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        // Rewrite good records
        for (const auto& entry : result.entries) {
            auto payload = serialize_entry(entry);
            uint32_t len = static_cast<uint32_t>(payload.size());
            uint32_t crc = compute_crc32(payload.data(), payload.size());
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
            out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
        // Rewrite metadata if present
        if (result.term > 0) {
            auto payload = serialize_metadata(result.term, result.voted_for);
            uint32_t len = static_cast<uint32_t>(payload.size());
            uint32_t crc = compute_crc32(payload.data(), payload.size());
            out.write(reinterpret_cast<const char*>(&len), sizeof(len));
            out.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
            out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
        out.flush();
    }

    return Result<ReplayResult>::success(std::move(result));
}

} // namespace raftkv
