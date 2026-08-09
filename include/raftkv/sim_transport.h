#pragma once

#include <deque>
#include <functional>
#include <optional>
#include <random>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "raftkv/memtable.h"
#include "raftkv/raft_node.h"
#include "raftkv/types.h"

namespace raftkv {

// Deterministic in-process transport for testing.
// Allows injecting partitions, delays, and message loss.
class SimTransport {
public:
    struct PendingMessage {
        Envelope envelope;
        Millis deliver_at;
    };

    explicit SimTransport(uint64_t seed = 42);

    // Add a message to the network
    void send(const Envelope& env);
    void send_at(const Envelope& env, Millis current_time);

    // Get messages deliverable at the given time
    std::vector<Envelope> deliver(Millis now);

    // Partition: messages between `a` and `b` are dropped
    void add_partition(NodeId a, NodeId b);
    void remove_partition(NodeId a, NodeId b);
    void heal_all();

    // Check if two nodes can communicate
    bool is_partitioned(NodeId from, NodeId to) const;

    // Set message delay range
    void set_delay(Millis min_delay, Millis max_delay);

    // Drop rate (0.0 to 1.0)
    void set_drop_rate(double rate);

    // Get pending message count
    size_t pending_count() const { return pending_.size(); }

private:
    std::deque<PendingMessage> pending_;
    std::set<std::pair<NodeId, NodeId>> partitions_;
    Millis min_delay_ = 1;
    Millis max_delay_ = 10;
    double drop_rate_ = 0.0;
    std::mt19937 rng_;
};

// A simulated cluster: multiple RaftNodes + MemTables over a SimTransport.
// All driven by a single thread with a virtual clock.
class SimCluster {
public:
    struct NodeState {
        std::unique_ptr<RaftNode> raft;
        MemTable memtable;
        bool alive = true;
        bool paused = false;
    };

    SimCluster(size_t num_nodes, uint64_t seed = 42);

    // Advance time by `delta` ms and deliver/tick all nodes
    void tick(Millis delta = 1);

    // Advance until a leader is elected or timeout
    bool wait_for_leader(Millis timeout_ms = 5000);

    // Get the current leader (if any)
    std::optional<NodeId> leader() const;

    // Propose a command through the leader
    bool propose(const Command& cmd);

    // Wait for a command to be applied on a majority
    bool wait_for_commit(LogIndex index, Millis timeout_ms = 5000);

    // Get value from a specific node's memtable
    std::optional<std::string> get(NodeId node, std::string_view key) const;

    // Partition a node from all others
    void isolate(NodeId node);

    // Partition between specific nodes
    void partition(NodeId a, NodeId b);

    // Heal all partitions
    void heal_all();

    // Kill a node (stops processing)
    void kill(NodeId node);

    // Restart a node (with persisted state)
    void restart(NodeId node);

    // Pause a node (stops ticking but stays "alive")
    void pause(NodeId node);

    // Resume a paused node
    void resume(NodeId node);

    // Access nodes directly
    NodeState& node(NodeId id) { return nodes_.at(id); }
    const NodeState& node(NodeId id) const { return nodes_.at(id); }

    // Current virtual time
    Millis now() const { return now_; }

    // Number of nodes
    size_t num_nodes() const { return nodes_.size(); }

    // Get all node IDs
    std::vector<NodeId> node_ids() const;

    // Check safety: no two nodes have different committed entries at same index
    bool check_safety() const;

    // Transport access for fine-grained control
    SimTransport& transport() { return transport_; }

private:
    void apply_entries(NodeId id, const std::vector<LogEntry>& entries);
    void deliver_messages(const std::vector<Envelope>& messages);

    std::unordered_map<NodeId, NodeState> nodes_;
    SimTransport transport_;
    Millis now_ = 0;
    uint64_t seed_;
};

} // namespace raftkv
