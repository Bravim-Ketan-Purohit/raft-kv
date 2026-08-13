#include "raftkv/grpc_transport.h"

#include <chrono>
#include <iostream>

// gRPC includes will be available after FetchContent provides them.
// For now this is a structural placeholder that compiles without gRPC
// and will be fleshed out when we wire gRPC FetchContent into CMake.
//
// The transport does NOT live in src/consensus/ — it is the bridge layer
// between the network and the pure RaftNode state machine.

namespace raftkv {

struct GrpcTransport::Impl {
    // Placeholder: actual gRPC server/stubs go here
    // This will be implemented once gRPC is available via FetchContent
};

GrpcTransport::GrpcTransport(NodeId self_id, const std::vector<NodeConfig>& cluster_config)
    : self_id_(self_id), cluster_config_(cluster_config), impl_(std::make_unique<Impl>()) {}

GrpcTransport::~GrpcTransport() {
    stop();
}

void GrpcTransport::start() {
    running_.store(true);
}

void GrpcTransport::stop() {
    running_.store(false);
}

void GrpcTransport::send(const Envelope& env) {
    (void)env;
    // Will be implemented with actual gRPC calls
}

} // namespace raftkv
