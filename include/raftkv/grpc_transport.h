#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "raftkv/raft_node.h"
#include "raftkv/types.h"

// Forward declarations for gRPC types
namespace grpc {
class Server;
class ServerBuilder;
class Channel;
} // namespace grpc

namespace raftkv {

// Configuration for a single node in the cluster
struct NodeConfig {
    NodeId id;
    std::string address; // host:port for gRPC peer
    std::string status_address; // host:port for HTTP status
};

// gRPC-based transport that bridges the network to the pure RaftNode state machine.
// This lives in src/transport/ and owns the networking; src/consensus/ stays pure.
class GrpcTransport {
public:
    GrpcTransport(NodeId self_id, const std::vector<NodeConfig>& cluster_config);
    ~GrpcTransport();

    // Start the gRPC server and peer connections
    void start();

    // Stop the transport
    void stop();

    // Send a message to a peer (non-blocking, best-effort)
    void send(const Envelope& env);

    // Check if transport is running
    bool running() const { return running_.load(); }

private:
    NodeId self_id_;
    std::vector<NodeConfig> cluster_config_;
    std::atomic<bool> running_{false};

    // The actual gRPC implementation lives in the .cpp to avoid header pollution
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace raftkv
