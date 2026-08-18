#include "raftkv/sim_transport.h"

#include <algorithm>
#include <cassert>

namespace raftkv {

// --- SimTransport ---

SimTransport::SimTransport(uint64_t seed) : rng_(seed) {}

void SimTransport::send(const Envelope& env) {
    send_at(env, 0);
}

void SimTransport::send_at(const Envelope& env, Millis current_time) {
    if (is_partitioned(env.from, env.to)) {
        return; // Message dropped due to partition
    }

    // Random drop
    if (drop_rate_ > 0.0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(rng_) < drop_rate_) {
            return;
        }
    }

    // Random delay
    std::uniform_int_distribution<Millis> delay_dist(min_delay_, max_delay_);
    Millis delay = delay_dist(rng_);

    pending_.push_back(PendingMessage{env, current_time + delay});
}

std::vector<Envelope> SimTransport::deliver(Millis now) {
    std::vector<Envelope> ready;

    // Adjust: pending messages have relative delays from when they were sent.
    // We need absolute delivery times. Let's fix: store absolute deliver_at.
    // For simplicity, we decrement pending delivery times each tick.
    auto it = pending_.begin();
    while (it != pending_.end()) {
        if (it->deliver_at <= now) {
            if (!is_partitioned(it->envelope.from, it->envelope.to)) {
                ready.push_back(it->envelope);
            }
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }

    return ready;
}

void SimTransport::add_partition(NodeId a, NodeId b) {
    partitions_.insert({a, b});
    partitions_.insert({b, a});
}

void SimTransport::remove_partition(NodeId a, NodeId b) {
    partitions_.erase({a, b});
    partitions_.erase({b, a});
}

void SimTransport::heal_all() {
    partitions_.clear();
}

bool SimTransport::is_partitioned(NodeId from, NodeId to) const {
    return partitions_.count({from, to}) > 0;
}

void SimTransport::set_delay(Millis min_delay, Millis max_delay) {
    min_delay_ = min_delay;
    max_delay_ = max_delay;
}

void SimTransport::set_drop_rate(double rate) {
    drop_rate_ = std::clamp(rate, 0.0, 1.0);
}

// --- SimCluster ---

SimCluster::SimCluster(size_t num_nodes, uint64_t seed)
    : transport_(seed), now_(0), seed_(seed) {
    std::vector<NodeId> all_ids;
    for (size_t i = 1; i <= num_nodes; ++i) {
        all_ids.push_back(static_cast<NodeId>(i));
    }

    for (size_t i = 1; i <= num_nodes; ++i) {
        NodeId id = static_cast<NodeId>(i);
        std::vector<NodeId> peers;
        for (NodeId other : all_ids) {
            if (other != id) peers.push_back(other);
        }

        PersistentState state;
        Rng rng(seed + i);
        auto raft = std::make_unique<RaftNode>(id, peers, state, rng);
        raft->set_election_timeout(150); // Faster for tests
        raft->set_heartbeat_interval(50);

        // NodeState holds a MemTable, which is non-copyable and (because it
        // declares a destructor) non-movable. Construct it in place rather
        // than moving a temporary in.
        auto& ns = nodes_[id];
        ns.raft = std::move(raft);
        ns.alive = true;
        ns.paused = false;
    }
}

void SimCluster::tick(Millis delta) {
    now_ += delta;

    // Deliver pending messages
    auto messages = transport_.deliver(now_);
    deliver_messages(messages);

    // Tick all alive nodes
    for (auto& [id, ns] : nodes_) {
        if (!ns.alive || ns.paused) continue;

        Output out = ns.raft->tick(now_);
        if (!out.to_apply.empty()) {
            apply_entries(id, out.to_apply);
        }
        for (const auto& env : out.messages) {
            transport_.send_at(env, now_);
        }
    }
}

bool SimCluster::wait_for_leader(Millis timeout_ms) {
    Millis deadline = now_ + timeout_ms;
    while (now_ < deadline) {
        tick(1);
        if (leader().has_value()) {
            return true;
        }
    }
    return false;
}

std::optional<NodeId> SimCluster::leader() const {
    NodeId found_leader = 0;
    Term highest_term = 0;

    for (const auto& [id, ns] : nodes_) {
        if (!ns.alive) continue;
        if (ns.raft->role() == Role::LEADER) {
            if (ns.raft->current_term() >= highest_term) {
                highest_term = ns.raft->current_term();
                found_leader = id;
            }
        }
    }

    if (found_leader > 0) return found_leader;
    return std::nullopt;
}

bool SimCluster::propose(const Command& cmd) {
    auto lid = leader();
    if (!lid.has_value()) return false;

    auto& ns = nodes_.at(lid.value());
    Output out = ns.raft->propose(cmd, now_);
    if (!out.to_apply.empty()) {
        apply_entries(lid.value(), out.to_apply);
    }
    for (const auto& env : out.messages) {
        transport_.send_at(env, now_);
    }
    return true;
}

bool SimCluster::wait_for_commit(LogIndex index, Millis timeout_ms) {
    Millis deadline = now_ + timeout_ms;
    while (now_ < deadline) {
        tick(1);

        // Check if majority has applied this index
        size_t applied_count = 0;
        for (const auto& [id, ns] : nodes_) {
            if (!ns.alive) continue;
            if (ns.raft->last_applied() >= index) {
                applied_count++;
            }
        }
        if (applied_count > nodes_.size() / 2) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> SimCluster::get(NodeId node, std::string_view key) const {
    auto it = nodes_.find(node);
    if (it == nodes_.end()) return std::nullopt;
    return it->second.memtable.get(key);
}

void SimCluster::isolate(NodeId node) {
    for (const auto& [id, _] : nodes_) {
        if (id != node) {
            transport_.add_partition(node, id);
        }
    }
}

void SimCluster::partition(NodeId a, NodeId b) {
    transport_.add_partition(a, b);
}

void SimCluster::heal_all() {
    transport_.heal_all();
}

void SimCluster::kill(NodeId node) {
    auto it = nodes_.find(node);
    if (it != nodes_.end()) {
        it->second.alive = false;
    }
}

void SimCluster::restart(NodeId node) {
    auto it = nodes_.find(node);
    if (it == nodes_.end()) return;

    // Get persistent state from the killed node
    PersistentState state = it->second.raft->persistent_state();

    // Rebuild peers list
    std::vector<NodeId> peers;
    for (const auto& [id, _] : nodes_) {
        if (id != node) peers.push_back(id);
    }

    // Create fresh RaftNode with restored state
    Rng rng(seed_ + node + now_); // Different seed to avoid deterministic replay
    auto raft = std::make_unique<RaftNode>(node, peers, state, rng);
    raft->set_election_timeout(150);
    raft->set_heartbeat_interval(50);

    it->second.raft = std::move(raft);
    it->second.alive = true;
    it->second.paused = false;

    // NOTE: committed entries are intentionally NOT replayed into the memtable
    // here. A restarted node re-derives its applied state from the leader via
    // AppendEntries once it rejoins, so the memtable refills through the normal
    // apply path. (Restart-from-WAL replay is exercised in the Wal tests.)
}

void SimCluster::pause(NodeId node) {
    auto it = nodes_.find(node);
    if (it != nodes_.end()) {
        it->second.paused = true;
    }
}

void SimCluster::resume(NodeId node) {
    auto it = nodes_.find(node);
    if (it != nodes_.end()) {
        it->second.paused = false;
    }
}

std::vector<NodeId> SimCluster::node_ids() const {
    std::vector<NodeId> ids;
    for (const auto& [id, _] : nodes_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool SimCluster::check_safety() const {
    // Safety invariant: no two nodes hold different entries at the same committed index
    for (const auto& [id_a, ns_a] : nodes_) {
        if (!ns_a.alive) continue;
        LogIndex commit_a = ns_a.raft->commit_index();

        for (const auto& [id_b, ns_b] : nodes_) {
            if (id_b <= id_a || !ns_b.alive) continue;
            LogIndex commit_b = ns_b.raft->commit_index();
            LogIndex min_commit = std::min(commit_a, commit_b);

            auto state_a = ns_a.raft->persistent_state();
            auto state_b = ns_b.raft->persistent_state();

            for (LogIndex i = 1; i <= min_commit; ++i) {
                if (i > state_a.log.size() || i > state_b.log.size()) break;
                const auto& entry_a = state_a.log[i - 1];
                const auto& entry_b = state_b.log[i - 1];
                if (entry_a.term != entry_b.term) {
                    return false;
                }
            }
        }
    }
    return true;
}

void SimCluster::apply_entries(NodeId id, const std::vector<LogEntry>& entries) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return;

    for (const auto& entry : entries) {
        if (std::holds_alternative<PutCommand>(entry.command)) {
            const auto& cmd = std::get<PutCommand>(entry.command);
            it->second.memtable.put(cmd.key, cmd.value);
        } else if (std::holds_alternative<DeleteCommand>(entry.command)) {
            const auto& cmd = std::get<DeleteCommand>(entry.command);
            it->second.memtable.remove(cmd.key);
        }
        // NoOp: nothing to apply to state machine
    }
}

void SimCluster::deliver_messages(const std::vector<Envelope>& messages) {
    for (const auto& env : messages) {
        auto it = nodes_.find(env.to);
        if (it == nodes_.end() || !it->second.alive || it->second.paused) continue;

        Output out = it->second.raft->step(env, now_);
        if (!out.to_apply.empty()) {
            apply_entries(env.to, out.to_apply);
        }
        for (const auto& msg : out.messages) {
            transport_.send_at(msg, now_);
        }
    }
}

} // namespace raftkv
