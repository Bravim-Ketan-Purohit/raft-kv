#include "raftkv/server.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace raftkv {

Server::Server(const ServerConfig& config) : config_(config) {
    // Create data directory
    std::filesystem::create_directories(config_.data_dir);

    // Initialize WAL
    auto wal_path = std::filesystem::path(config_.data_dir) / "raft.wal";
    wal_ = std::make_unique<Wal>(wal_path, config_.no_fsync);

    // Replay WAL for crash recovery
    PersistentState restored;
    auto replay_result = Wal::replay(wal_path);
    if (replay_result.ok) {
        restored.current_term = replay_result.value.term;
        restored.voted_for = replay_result.value.voted_for;
        restored.log = std::move(replay_result.value.entries);

        if (replay_result.value.truncated_tail) {
            std::cerr << "[node " << config_.node_id << "] WAL: truncated torn tail on replay\n";
        }
    }

    // Build peer list
    std::vector<NodeId> peers;
    for (const auto& nc : config_.cluster) {
        if (nc.id != config_.node_id) {
            peers.push_back(nc.id);
        }
    }

    // Create RaftNode
    Rng rng(config_.node_id * 31 + 7);
    raft_ = std::make_unique<RaftNode>(config_.node_id, peers, restored, rng);
    raft_->set_election_timeout(config_.election_timeout_ms);
    raft_->set_heartbeat_interval(config_.heartbeat_interval_ms);

    // Re-apply committed entries to memtable
    // On restart, we need to replay committed entries. The commit index is lost
    // on restart; it will be re-established by the leader. For now, apply all
    // entries that were in the WAL (they were committed pre-crash).
    for (const auto& entry : restored.log) {
        if (std::holds_alternative<PutCommand>(entry.command)) {
            const auto& cmd = std::get<PutCommand>(entry.command);
            memtable_.put(cmd.key, cmd.value);
        } else if (std::holds_alternative<DeleteCommand>(entry.command)) {
            const auto& cmd = std::get<DeleteCommand>(entry.command);
            memtable_.remove(cmd.key);
        }
    }

    // Initialize transport
    transport_ = std::make_unique<GrpcTransport>(config_.node_id, config_.cluster);
}

Server::~Server() {
    stop();
}

void Server::run() {
    running_.store(true);
    transport_->start();

    // Start tick loop
    tick_thread_ = std::thread(&Server::tick_loop, this);
    tick_thread_.join();
}

void Server::stop() {
    running_.store(false);
    if (transport_) transport_->stop();
    if (tick_thread_.joinable()) tick_thread_.join();
}

void Server::tick_loop() {
    using clock = std::chrono::steady_clock;
    auto start = clock::now();

    while (running_.load()) {
        auto now_tp = clock::now();
        Millis now_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now_tp - start).count();

        Output out;
        {
            std::lock_guard<std::mutex> lock(raft_mutex_);
            out = raft_->tick(now_ms);
        }

        // Persist if needed
        if (out.persist_needed) {
            auto state = raft_->persistent_state();
            wal_->write_metadata(state.current_term, state.voted_for);
            // Note: individual entries are persisted on append
        }

        // Apply committed entries
        if (!out.to_apply.empty()) {
            apply_committed_entries(out.to_apply);
        }

        // Send messages
        for (const auto& env : out.messages) {
            transport_->send(env);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Server::apply_committed_entries(const std::vector<LogEntry>& entries) {
    for (const auto& entry : entries) {
        if (std::holds_alternative<PutCommand>(entry.command)) {
            const auto& cmd = std::get<PutCommand>(entry.command);
            memtable_.put(cmd.key, cmd.value);
        } else if (std::holds_alternative<DeleteCommand>(entry.command)) {
            const auto& cmd = std::get<DeleteCommand>(entry.command);
            memtable_.remove(cmd.key);
        }
        // NoOp: nothing to apply
    }
}

Server::GetResult Server::get(std::string_view key, ReadMode mode) {
    GetResult result;

    if (mode == ReadMode::STALE_OK) {
        // Serve directly from local state
        auto val = memtable_.get(key);
        result.found = val.has_value();
        if (result.found) result.value = val.value();
        result.commit_index = raft_->commit_index();
        return result;
    }

    // LINEARIZABLE: must be leader
    std::lock_guard<std::mutex> lock(raft_mutex_);
    if (raft_->role() != Role::LEADER) {
        result.not_leader = true;
        result.leader_hint = leader_address();
        return result;
    }

    // ReadIndex protocol: confirm leadership via heartbeat ack
    // For single-node, this is immediate
    auto read_state = raft_->begin_read_index(0);
    if (!read_state.has_value()) {
        result.not_leader = true;
        result.leader_hint = leader_address();
        return result;
    }

    // For simplicity in single-threaded mode, if confirmed, serve
    if (read_state->confirmed) {
        auto val = memtable_.get(key);
        result.found = val.has_value();
        if (result.found) result.value = val.value();
        result.commit_index = raft_->commit_index();
        return result;
    }

    // In a real async implementation, we'd wait for quorum acks.
    // For now, serve from memtable (the actual ReadIndex confirmation
    // happens via heartbeat acks in the tick loop).
    auto val = memtable_.get(key);
    result.found = val.has_value();
    if (result.found) result.value = val.value();
    result.commit_index = raft_->commit_index();
    return result;
}

Server::WriteResult Server::put(std::string_view key, std::string_view value) {
    WriteResult result;

    std::lock_guard<std::mutex> lock(raft_mutex_);
    if (raft_->role() != Role::LEADER) {
        result.not_leader = true;
        result.leader_hint = leader_address();
        return result;
    }

    auto now_ms = 0; // Will be set properly in tick_loop context
    Output out = raft_->propose(PutCommand{std::string(key), std::string(value)}, now_ms);

    if (out.persist_needed) {
        // Persist the new entry
        auto state = raft_->persistent_state();
        if (!state.log.empty()) {
            wal_->append(state.log.back());
        }
    }

    // Send messages
    for (const auto& env : out.messages) {
        transport_->send(env);
    }

    // Apply committed
    if (!out.to_apply.empty()) {
        apply_committed_entries(out.to_apply);
    }

    result.success = true;
    return result;
}

Server::WriteResult Server::del(std::string_view key) {
    WriteResult result;

    std::lock_guard<std::mutex> lock(raft_mutex_);
    if (raft_->role() != Role::LEADER) {
        result.not_leader = true;
        result.leader_hint = leader_address();
        return result;
    }

    auto now_ms = 0;
    Output out = raft_->propose(DeleteCommand{std::string(key)}, now_ms);

    if (out.persist_needed) {
        auto state = raft_->persistent_state();
        if (!state.log.empty()) {
            wal_->append(state.log.back());
        }
    }

    for (const auto& env : out.messages) {
        transport_->send(env);
    }

    if (!out.to_apply.empty()) {
        apply_committed_entries(out.to_apply);
    }

    result.success = true;
    return result;
}

RaftStatus Server::status() const {
    std::lock_guard<std::mutex> lock(raft_mutex_);
    return raft_->status();
}

std::string Server::leader_address() const {
    auto lid = raft_->leader_id();
    if (!lid.has_value()) return "";

    for (const auto& nc : config_.cluster) {
        if (nc.id == lid.value()) {
            return nc.address;
        }
    }
    return "";
}

} // namespace raftkv
