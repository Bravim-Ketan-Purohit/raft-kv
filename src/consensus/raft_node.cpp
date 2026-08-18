#include "raftkv/raft_node.h"

#include <algorithm>
#include <cassert>

namespace raftkv {

RaftNode::RaftNode(NodeId self, std::vector<NodeId> peers, PersistentState restored, Rng rng)
    : self_(self), peers_(std::move(peers)), rng_(rng) {
    current_term_ = restored.current_term;
    voted_for_ = restored.voted_for;
    if (!restored.log.empty()) {
        log_.restore(std::move(restored.log));
    }
}

Output RaftNode::tick(Millis now) {
    if (start_time_ == 0) {
        start_time_ = now;
        reset_election_timer(now);
    }

    Output out;

    if (role_ == Role::LEADER) {
        // Send heartbeats
        if (now >= last_heartbeat_time_ + heartbeat_interval_) {
            last_heartbeat_time_ = now;
            for (NodeId peer : peers_) {
                send_append_entries(peer, out, now);
            }
        }
    } else {
        // Check election timeout
        if (now >= election_deadline_) {
            out = start_election(now);
        }
    }

    apply_committed(out);
    return out;
}

Output RaftNode::step(const Envelope& envelope, Millis now) {
    if (start_time_ == 0) {
        start_time_ = now;
        reset_election_timer(now);
    }

    Output out;

    // Check for higher term in any message
    Term msg_term = 0;
    std::visit(
        [&msg_term](const auto& msg) {
            if constexpr (requires { msg.term; }) {
                msg_term = msg.term;
            }
        },
        envelope.msg);

    if (msg_term > current_term_) {
        become_follower(msg_term, now);
        out.persist_needed = true;
    }

    std::visit(
        [&](const auto& msg) {
            using T = std::decay_t<decltype(msg)>;
            if constexpr (std::is_same_v<T, VoteRequest>) {
                out = handle_vote_request(envelope.from, msg, now);
            } else if constexpr (std::is_same_v<T, VoteReply>) {
                out = handle_vote_reply(envelope.from, msg, now);
            } else if constexpr (std::is_same_v<T, AppendRequest>) {
                out = handle_append_request(envelope.from, msg, now);
            } else if constexpr (std::is_same_v<T, AppendReply>) {
                out = handle_append_reply(envelope.from, msg, now);
            }
        },
        envelope.msg);

    apply_committed(out);
    return out;
}

Output RaftNode::propose(const Command& cmd, Millis now) {
    Output out;

    if (role_ != Role::LEADER) {
        return out; // Only leader can propose
    }

    log_.append(current_term_, cmd);
    out.persist_needed = true;

    // Immediately try to replicate
    for (NodeId peer : peers_) {
        send_append_entries(peer, out, now);
    }

    // Single node cluster: commit immediately
    if (peers_.empty()) {
        advance_commit_index();
        apply_committed(out);
    }

    return out;
}

RaftStatus RaftNode::status() const {
    RaftStatus s;
    s.id = self_;
    s.role = role_;
    s.term = current_term_;
    s.voted_for = voted_for_;
    s.commit_index = commit_index_;
    s.last_applied = last_applied_;
    s.log_length = log_.last_index();
    s.leader_id = leader_id_;
    s.uptime_ms = 0; // Caller provides actual uptime
    s.peers = peer_state_;
    return s;
}

PersistentState RaftNode::persistent_state() const {
    PersistentState state;
    state.current_term = current_term_;
    state.voted_for = voted_for_;
    state.log = log_.entries_from(1);
    return state;
}

std::optional<RaftNode::ReadIndexState> RaftNode::begin_read_index(Millis now) {
    (void)now;
    if (role_ != Role::LEADER) {
        return std::nullopt;
    }

    size_t quorum = (peers_.size() + 1) / 2 + 1; // Majority including self
    ReadIndexState state;
    state.read_index = commit_index_;
    state.acks_needed = quorum - 1; // Self already counts
    state.acks_received = 0;
    state.confirmed = (peers_.empty()); // Single node: immediately confirmed

    read_index_state_ = state;
    return state;
}

void RaftNode::ack_read_index(NodeId from) {
    (void)from;
    if (!read_index_state_) return;
    read_index_state_->acks_received++;
    if (read_index_state_->acks_received >= read_index_state_->acks_needed) {
        read_index_state_->confirmed = true;
    }
}

// --- Private ---

void RaftNode::become_follower(Term term, Millis now) {
    role_ = Role::FOLLOWER;
    current_term_ = term;
    voted_for_ = std::nullopt;
    leader_id_ = std::nullopt;
    votes_received_.clear();
    read_index_state_ = std::nullopt;
    reset_election_timer(now);
}

void RaftNode::become_candidate(Millis now) {
    role_ = Role::CANDIDATE;
    current_term_++;
    voted_for_ = self_;
    leader_id_ = std::nullopt;
    votes_received_.clear();
    votes_received_[self_] = true;
    read_index_state_ = std::nullopt;
    reset_election_timer(now);
}

void RaftNode::become_leader(Millis now) {
    role_ = Role::LEADER;
    leader_id_ = self_;
    read_index_state_ = std::nullopt;

    // Initialize peer state
    peer_state_.clear();
    for (NodeId peer : peers_) {
        PeerState ps;
        ps.next_index = log_.last_index() + 1;
        ps.match_index = 0;
        ps.last_ack_time = now;
        peer_state_[peer] = ps;
    }

    last_heartbeat_time_ = now;

    // Append no-op entry to establish commit index (Section 5.4.2)
    log_.append(current_term_, NoOpCommand{});
}

void RaftNode::reset_election_timer(Millis now) {
    election_deadline_ = now + random_election_timeout();
}

Millis RaftNode::random_election_timeout() {
    std::uniform_int_distribution<Millis> dist(election_timeout_base_,
                                               2 * election_timeout_base_);
    return dist(rng_);
}

Output RaftNode::start_election(Millis now) {
    Output out;
    become_candidate(now);
    out.persist_needed = true;

    // Single node: win immediately
    if (peers_.empty()) {
        become_leader(now);
        return out;
    }

    // Request votes from all peers
    VoteRequest req;
    req.term = current_term_;
    req.candidate_id = self_;
    req.last_log_index = log_.last_index();
    req.last_log_term = log_.last_term();

    for (NodeId peer : peers_) {
        out.messages.push_back(Envelope{self_, peer, req});
    }

    return out;
}

void RaftNode::send_append_entries(NodeId peer, Output& out, Millis now) {
    (void)now;
    auto it = peer_state_.find(peer);
    if (it == peer_state_.end()) return;

    PeerState& ps = it->second;
    LogIndex prev_index = ps.next_index - 1;
    Term prev_term = log_.term_at(prev_index);

    AppendRequest req;
    req.term = current_term_;
    req.leader_id = self_;
    req.prev_log_index = prev_index;
    req.prev_log_term = prev_term;
    req.leader_commit = commit_index_;

    // Send entries from next_index onwards
    if (ps.next_index <= log_.last_index()) {
        req.entries = log_.entries_from(ps.next_index);
    }

    out.messages.push_back(Envelope{self_, peer, req});
}

void RaftNode::advance_commit_index() {
    if (role_ != Role::LEADER) return;

    // Find the highest index replicated on a majority AND whose term == currentTerm
    // (Figure 8 safety condition)
    for (LogIndex n = log_.last_index(); n > commit_index_; --n) {
        if (log_.term_at(n) != current_term_) {
            continue; // Can only commit entries from current term
        }

        size_t count = 1; // Self
        for (const auto& [peer_id, ps] : peer_state_) {
            if (ps.match_index >= n) {
                count++;
            }
        }

        size_t quorum = (peers_.size() + 1) / 2 + 1;
        if (count >= quorum) {
            commit_index_ = n;
            break;
        }
    }
}

Output RaftNode::handle_vote_request(NodeId from, const VoteRequest& req, Millis now) {
    Output out;

    VoteReply reply;
    reply.term = current_term_;
    reply.vote_granted = false;

    // Reject if our term is higher
    if (req.term < current_term_) {
        out.messages.push_back(Envelope{self_, from, reply});
        return out;
    }

    // Grant vote if we haven't voted for someone else in this term,
    // and the candidate's log is at least as up-to-date as ours
    bool can_vote = (!voted_for_.has_value() || voted_for_.value() == req.candidate_id);
    // Candidate's log must be at least as up-to-date as ours:
    // compare last term first, then last index.
    Term my_last_term = log_.last_term();
    LogIndex my_last_index = log_.last_index();
    bool candidate_up_to_date = (req.last_log_term > my_last_term) ||
                                (req.last_log_term == my_last_term &&
                                 req.last_log_index >= my_last_index);

    if (can_vote && candidate_up_to_date) {
        reply.vote_granted = true;
        voted_for_ = req.candidate_id;
        out.persist_needed = true;
        reset_election_timer(now);
    }

    out.messages.push_back(Envelope{self_, from, reply});
    return out;
}

Output RaftNode::handle_vote_reply(NodeId from, const VoteReply& reply, Millis now) {
    Output out;

    if (role_ != Role::CANDIDATE) {
        return out;
    }

    if (reply.term > current_term_) {
        become_follower(reply.term, now);
        out.persist_needed = true;
        return out;
    }

    if (reply.vote_granted) {
        votes_received_[from] = true;
    }

    // Check if we have a majority
    size_t quorum = (peers_.size() + 1) / 2 + 1;
    size_t votes = 0;
    for (const auto& [_, granted] : votes_received_) {
        if (granted) votes++;
    }

    if (votes >= quorum) {
        become_leader(now);
        // Send initial heartbeats
        for (NodeId peer : peers_) {
            send_append_entries(peer, out, now);
        }
        out.persist_needed = true;
    }

    return out;
}

Output RaftNode::handle_append_request(NodeId from, const AppendRequest& req, Millis now) {
    Output out;

    AppendReply reply;
    reply.term = current_term_;
    reply.success = false;
    reply.match_index = 0;

    // Reject if our term is higher
    if (req.term < current_term_) {
        out.messages.push_back(Envelope{self_, from, reply});
        return out;
    }

    // Valid AppendEntries from current leader
    leader_id_ = req.leader_id;
    if (role_ == Role::CANDIDATE) {
        become_follower(req.term, now);
        out.persist_needed = true;
    }
    reset_election_timer(now);

    // Log consistency check
    if (req.prev_log_index > 0) {
        if (req.prev_log_index > log_.last_index()) {
            // We don't have the entry at prevLogIndex
            reply.conflict_index = log_.last_index() + 1;
            reply.conflict_term = std::nullopt;
            out.messages.push_back(Envelope{self_, from, reply});
            return out;
        }

        Term entry_term = log_.term_at(req.prev_log_index);
        if (entry_term != req.prev_log_term) {
            // Term mismatch — provide conflict info for fast backtrack
            reply.conflict_term = entry_term;
            // Find first index of the conflict term
            LogIndex conflict_start = req.prev_log_index;
            while (conflict_start > 1 && log_.term_at(conflict_start - 1) == entry_term) {
                conflict_start--;
            }
            reply.conflict_index = conflict_start;
            out.messages.push_back(Envelope{self_, from, reply});
            return out;
        }
    }

    // Append new entries (truncate conflicting suffix)
    for (size_t i = 0; i < req.entries.size(); ++i) {
        LogIndex idx = req.prev_log_index + 1 + i;
        if (idx <= log_.last_index()) {
            if (log_.term_at(idx) != req.entries[i].term) {
                // Conflict: truncate from here
                log_.truncate_from(idx);
                // Append this and remaining entries
                for (size_t j = i; j < req.entries.size(); ++j) {
                    log_.append(req.entries[j].term, req.entries[j].command);
                }
                out.persist_needed = true;
                break;
            }
        } else {
            // Append new entry
            log_.append(req.entries[i].term, req.entries[i].command);
            out.persist_needed = true;
        }
    }

    // Update commit index
    if (req.leader_commit > commit_index_) {
        commit_index_ = std::min(req.leader_commit, log_.last_index());
    }

    reply.success = true;
    reply.match_index = log_.last_index();
    out.messages.push_back(Envelope{self_, from, reply});
    return out;
}

Output RaftNode::handle_append_reply(NodeId from, const AppendReply& reply, Millis now) {
    Output out;
    (void)now;

    if (role_ != Role::LEADER) {
        return out;
    }

    auto it = peer_state_.find(from);
    if (it == peer_state_.end()) return out;

    PeerState& ps = it->second;

    if (reply.success) {
        ps.match_index = reply.match_index;
        ps.next_index = reply.match_index + 1;
        ps.last_ack_time = now;
        advance_commit_index();

        // Ack for ReadIndex protocol
        ack_read_index(from);
    } else {
        // Fast backtrack using conflict information
        if (reply.conflict_term.has_value()) {
            // Search our log for the conflict term
            Term ct = reply.conflict_term.value();
            LogIndex new_next = reply.conflict_index.value_or(1);

            // If we have entries with the conflict term, skip past them
            for (LogIndex i = log_.last_index(); i >= 1; --i) {
                if (log_.term_at(i) == ct) {
                    new_next = i + 1;
                    break;
                }
                if (log_.term_at(i) < ct) break;
            }
            ps.next_index = new_next;
        } else if (reply.conflict_index.has_value()) {
            ps.next_index = reply.conflict_index.value();
        } else {
            // Simple decrement
            if (ps.next_index > 1) {
                ps.next_index--;
            }
        }

        // Retry with updated nextIndex
        send_append_entries(from, out, now);
    }

    return out;
}

void RaftNode::apply_committed(Output& out) {
    while (last_applied_ < commit_index_) {
        last_applied_++;
        auto entry = log_.entry_at(last_applied_);
        if (entry.has_value()) {
            out.to_apply.push_back(entry.value());
        }
    }
}

} // namespace raftkv
