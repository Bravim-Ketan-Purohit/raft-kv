#include <gtest/gtest.h>

#include "raftkv/raft_node.h"

namespace raftkv {
namespace {

class RaftNodeTest : public ::testing::Test {
protected:
    std::unique_ptr<RaftNode> make_node(NodeId id, std::vector<NodeId> peers,
                                        uint64_t seed = 42) {
        PersistentState state;
        Rng rng(seed);
        auto node = std::make_unique<RaftNode>(id, peers, state, rng);
        node->set_election_timeout(150);
        node->set_heartbeat_interval(50);
        return node;
    }
};

TEST_F(RaftNodeTest, StartsAsFollower) {
    auto node = make_node(1, {2, 3});
    auto out = node->tick(0);
    EXPECT_EQ(node->role(), Role::FOLLOWER);
    EXPECT_EQ(node->current_term(), 0u);
}

TEST_F(RaftNodeTest, ElectionTimeoutTriggersCandidate) {
    auto node = make_node(1, {2, 3});
    node->tick(0); // Initialize

    // Advance past election timeout
    Output out;
    for (Millis t = 1; t <= 1000; t++) {
        out = node->tick(t);
        if (node->role() == Role::CANDIDATE) break;
    }

    EXPECT_EQ(node->role(), Role::CANDIDATE);
    EXPECT_EQ(node->current_term(), 1u);
    // Should have sent VoteRequest to all peers
    EXPECT_EQ(out.messages.size(), 2u);
}

TEST_F(RaftNodeTest, WinsElectionWithMajority) {
    auto node = make_node(1, {2, 3});
    node->tick(0);

    // Advance to election
    Millis t = 0;
    for (; t <= 1000; t++) {
        node->tick(t);
        if (node->role() == Role::CANDIDATE) break;
    }

    ASSERT_EQ(node->role(), Role::CANDIDATE);
    Term election_term = node->current_term();

    // Send vote from node 2
    VoteReply reply;
    reply.term = election_term;
    reply.vote_granted = true;

    Envelope env{2, 1, reply};
    auto out = node->step(env, t + 1);

    EXPECT_EQ(node->role(), Role::LEADER);
}

TEST_F(RaftNodeTest, RejectsVoteWithStaleTerm) {
    auto node = make_node(1, {2, 3});
    node->tick(0);

    // Manually advance term
    // Trigger an election first
    Millis t = 0;
    for (; t <= 1000; t++) {
        node->tick(t);
        if (node->role() == Role::CANDIDATE) break;
    }

    // Now node is in term 1
    VoteRequest req;
    req.term = 0; // Stale term
    req.candidate_id = 2;
    req.last_log_index = 0;
    req.last_log_term = 0;

    Envelope env{2, 1, req};
    auto out = node->step(env, t + 1);

    // Should reject
    ASSERT_EQ(out.messages.size(), 1u);
    auto& reply = std::get<VoteReply>(out.messages[0].msg);
    EXPECT_FALSE(reply.vote_granted);
}

TEST_F(RaftNodeTest, GrantsVoteWithUpToDateLog) {
    auto node = make_node(1, {2, 3});
    node->tick(0);

    VoteRequest req;
    req.term = 1;
    req.candidate_id = 2;
    req.last_log_index = 0;
    req.last_log_term = 0;

    Envelope env{2, 1, req};
    auto out = node->step(env, 1);

    ASSERT_EQ(out.messages.size(), 1u);
    auto& reply = std::get<VoteReply>(out.messages[0].msg);
    EXPECT_TRUE(reply.vote_granted);
    EXPECT_EQ(node->current_term(), 1u);
}

TEST_F(RaftNodeTest, StepsDownOnHigherTerm) {
    auto node = make_node(1, {2, 3});
    node->tick(0);

    // Make node a candidate first
    Millis t = 0;
    for (; t <= 1000; t++) {
        node->tick(t);
        if (node->role() == Role::CANDIDATE) break;
    }

    // Win election
    VoteReply vr;
    vr.term = node->current_term();
    vr.vote_granted = true;
    node->step(Envelope{2, 1, vr}, t + 1);
    ASSERT_EQ(node->role(), Role::LEADER);

    // Receive message with higher term
    AppendRequest ar;
    ar.term = node->current_term() + 5;
    ar.leader_id = 3;
    ar.prev_log_index = 0;
    ar.prev_log_term = 0;
    ar.leader_commit = 0;

    node->step(Envelope{3, 1, ar}, t + 2);
    EXPECT_EQ(node->role(), Role::FOLLOWER);
}

TEST_F(RaftNodeTest, SingleNodeElectsImmediately) {
    auto node = make_node(1, {});
    node->tick(0);

    // With no peers, should become leader on election timeout
    Millis t = 0;
    for (; t <= 1000; t++) {
        node->tick(t);
        if (node->role() == Role::LEADER) break;
    }

    EXPECT_EQ(node->role(), Role::LEADER);
}

TEST_F(RaftNodeTest, LeaderAppendsNoOpOnElection) {
    auto node = make_node(1, {});
    node->tick(0);

    // Win election
    Millis t = 0;
    for (; t <= 1000; t++) {
        node->tick(t);
        if (node->role() == Role::LEADER) break;
    }

    ASSERT_EQ(node->role(), Role::LEADER);
    auto state = node->persistent_state();
    // Should have at least the no-op entry
    EXPECT_GE(state.log.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<NoOpCommand>(state.log.back().command));
}

TEST_F(RaftNodeTest, ProposeOnFollowerDoesNothing) {
    auto node = make_node(1, {2, 3});
    node->tick(0);

    auto out = node->propose(PutCommand{"key", "val"}, 1);
    EXPECT_TRUE(out.messages.empty());
    EXPECT_TRUE(out.to_apply.empty());
}

TEST_F(RaftNodeTest, VotesOnlyOncePerTerm) {
    auto node = make_node(1, {2, 3});
    node->tick(0);

    // Vote for node 2
    VoteRequest req2;
    req2.term = 1;
    req2.candidate_id = 2;
    req2.last_log_index = 0;
    req2.last_log_term = 0;

    auto out1 = node->step(Envelope{2, 1, req2}, 1);
    auto& reply1 = std::get<VoteReply>(out1.messages[0].msg);
    EXPECT_TRUE(reply1.vote_granted);

    // Now node 3 asks for vote in same term
    VoteRequest req3;
    req3.term = 1;
    req3.candidate_id = 3;
    req3.last_log_index = 0;
    req3.last_log_term = 0;

    auto out2 = node->step(Envelope{3, 1, req3}, 2);
    auto& reply2 = std::get<VoteReply>(out2.messages[0].msg);
    EXPECT_FALSE(reply2.vote_granted);
}

} // namespace
} // namespace raftkv
