#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "raftkv/raft_log.h"
#include "raftkv/types.h"

namespace raftkv {

// Message types for peer communication
struct VoteRequest {
    Term term;
    NodeId candidate_id;
    LogIndex last_log_index;
    Term last_log_term;
};

struct VoteReply {
    Term term;
    bool vote_granted;
};

struct AppendRequest {
    Term term;
    NodeId leader_id;
    LogIndex prev_log_index;
    Term prev_log_term;
    std::vector<LogEntry> entries;
    LogIndex leader_commit;
};

struct AppendReply {
    Term term;
    bool success;
    LogIndex match_index; // On success: last index replicated
    // On failure: hint for fast backtrack
    std::optional<Term> conflict_term;
    std::optional<LogIndex> conflict_index;
};

using Message = std::variant<VoteRequest, VoteReply, AppendRequest, AppendReply>;

struct Envelope {
    NodeId from;
    NodeId to;
    Message msg;
};

// Persistent state that survives restart
struct PersistentState {
    Term current_term = 0;
    std::optional<NodeId> voted_for;
    std::vector<LogEntry> log;
};

// Per-peer replication state (leader only)
struct PeerState {
    LogIndex next_index = 1;
    LogIndex match_index = 0;
    Millis last_ack_time = 0;
};

// Status for external inspection
struct RaftStatus {
    NodeId id;
    Role role;
    Term term;
    std::optional<NodeId> voted_for;
    LogIndex commit_index;
    LogIndex last_applied;
    LogIndex log_length;
    std::optional<NodeId> leader_id;
    Millis uptime_ms;
    std::unordered_map<NodeId, PeerState> peers;
};

// Output from a tick/step/propose call
struct Output {
    std::vector<Envelope> messages;
    std::vector<LogEntry> to_apply;
    bool persist_needed = false;
};

// RNG type injected for deterministic testing
using Rng = std::mt19937;

// Pure state machine implementing Raft consensus.
// NO sockets, NO threads, NO wall clock. Time and randomness are parameters.
class RaftNode {
public:
    RaftNode(NodeId self, std::vector<NodeId> peers, PersistentState restored, Rng rng);

    // Advance timers (election timeout, heartbeat)
    Output tick(Millis now);

    // Process an inbound peer message
    Output step(const Envelope& envelope, Millis now);

    // Propose a new command (client write). Only valid on leader.
    Output propose(const Command& cmd, Millis now);

    // Get current status
    RaftStatus status() const;

    // Get persistent state for WAL
    PersistentState persistent_state() const;

    // Get the commit index
    LogIndex commit_index() const { return commit_index_; }

    // Get last applied index
    LogIndex last_applied() const { return last_applied_; }

    // Get current role
    Role role() const { return role_; }

    // Get current term
    Term current_term() const { return current_term_; }

    // Get current leader
    std::optional<NodeId> leader_id() const { return leader_id_; }

    // Configuration
    void set_election_timeout(Millis base_ms) { election_timeout_base_ = base_ms; }
    void set_heartbeat_interval(Millis ms) { heartbeat_interval_ = ms; }

    // ReadIndex support: check if we're still the leader via quorum ack
    struct ReadIndexState {
        LogIndex read_index;
        size_t acks_needed;
        size_t acks_received;
        bool confirmed;
    };
    std::optional<ReadIndexState> begin_read_index(Millis now);
    void ack_read_index(NodeId from);

private:
    // State transitions
    void become_follower(Term term, Millis now);
    void become_candidate(Millis now);
    void become_leader(Millis now);

    // Election
    void reset_election_timer(Millis now);
    Millis random_election_timeout();
    Output start_election(Millis now);

    // Replication
    void send_append_entries(NodeId peer, Output& out, Millis now);
    void advance_commit_index();

    // Message handlers
    Output handle_vote_request(NodeId from, const VoteRequest& req, Millis now);
    Output handle_vote_reply(NodeId from, const VoteReply& reply, Millis now);
    Output handle_append_request(NodeId from, const AppendRequest& req, Millis now);
    Output handle_append_reply(NodeId from, const AppendReply& reply, Millis now);

    // Apply committed entries
    void apply_committed(Output& out);

    // Node identity
    NodeId self_;
    std::vector<NodeId> peers_;

    // Persistent state
    Term current_term_ = 0;
    std::optional<NodeId> voted_for_;
    RaftLog log_;

    // Volatile state
    Role role_ = Role::FOLLOWER;
    LogIndex commit_index_ = 0;
    LogIndex last_applied_ = 0;
    std::optional<NodeId> leader_id_;

    // Leader state
    std::unordered_map<NodeId, PeerState> peer_state_;

    // Election state
    std::unordered_map<NodeId, bool> votes_received_;
    Millis election_deadline_ = 0;
    Millis last_heartbeat_time_ = 0;

    // Timing configuration
    Millis election_timeout_base_ = 300;
    Millis heartbeat_interval_ = 100;

    // Uptime tracking
    Millis start_time_ = 0;

    // RNG for randomized timeouts
    Rng rng_;

    // ReadIndex state
    std::optional<ReadIndexState> read_index_state_;
};

} // namespace raftkv
