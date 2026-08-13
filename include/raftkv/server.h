#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "raftkv/grpc_transport.h"
#include "raftkv/memtable.h"
#include "raftkv/raft_node.h"
#include "raftkv/types.h"
#include "raftkv/wal.h"

namespace raftkv {

// Server configuration
struct ServerConfig {
    NodeId node_id;
    std::vector<NodeConfig> cluster;
    std::string data_dir;             // WAL directory
    bool no_fsync = false;            // For benchmarks
    bool enable_admin = false;        // Enable chaos admin endpoints
    Millis election_timeout_ms = 300; // Base election timeout
    Millis heartbeat_interval_ms = 100;
    std::string otel_exporter = "none"; // "none" | "otlp"
};

// The main server that wires together Raft, storage, transport, and the HTTP status API.
class Server {
public:
    explicit Server(const ServerConfig& config);
    ~Server();

    // Start the server (blocks until stopped)
    void run();

    // Stop the server
    void stop();

    // Client KV operations (routed through Raft)
    struct GetResult {
        bool found = false;
        std::string value;
        bool not_leader = false;
        std::string leader_hint;
        LogIndex commit_index = 0;
    };

    struct WriteResult {
        bool success = false;
        bool not_leader = false;
        std::string leader_hint;
    };

    GetResult get(std::string_view key, ReadMode mode);
    WriteResult put(std::string_view key, std::string_view value);
    WriteResult del(std::string_view key);

    // Status
    RaftStatus status() const;

private:
    void tick_loop();
    void apply_committed_entries(const std::vector<LogEntry>& entries);
    std::string leader_address() const;

    ServerConfig config_;
    std::unique_ptr<RaftNode> raft_;
    MemTable memtable_;
    std::unique_ptr<Wal> wal_;
    std::unique_ptr<GrpcTransport> transport_;

    std::atomic<bool> running_{false};
    std::thread tick_thread_;
    mutable std::mutex raft_mutex_; // Protects raft_ state machine (single writer)
};

} // namespace raftkv
