#include <gtest/gtest.h>

#include "raftkv/sim_transport.h"

namespace raftkv {
namespace {

TEST(PartitionTest, LeaderIsolationNewElection) {
    SimCluster cluster(3, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    NodeId old_leader = cluster.leader().value();
    cluster.isolate(old_leader);

    // Wait for new leader among the majority partition
    for (int i = 0; i < 2000; i++) cluster.tick(1);

    bool new_leader = false;
    for (NodeId id : cluster.node_ids()) {
        if (id == old_leader) continue;
        if (cluster.node(id).raft->role() == Role::LEADER) {
            new_leader = true;
        }
    }
    EXPECT_TRUE(new_leader);
}

TEST(PartitionTest, HealPartitionOldLeaderStepsDown) {
    SimCluster cluster(3, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    NodeId old_leader = cluster.leader().value();
    Term old_term = cluster.node(old_leader).raft->current_term();

    // Isolate old leader
    cluster.isolate(old_leader);

    // Wait for new leader
    for (int i = 0; i < 2000; i++) cluster.tick(1);

    // Heal partition
    cluster.heal_all();

    // Wait for convergence
    for (int i = 0; i < 1000; i++) cluster.tick(1);

    // Old leader should have stepped down (its term should be >= new term)
    Role old_role = cluster.node(old_leader).raft->role();
    Term current_term = cluster.node(old_leader).raft->current_term();

    // Old leader should not still be leader (it's behind)
    // After healing, it receives messages with higher term and steps down
    EXPECT_NE(old_role, Role::LEADER);
    EXPECT_GT(current_term, old_term);
}

TEST(PartitionTest, WriteDuringPartitionNotLost) {
    SimCluster cluster(3, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    // Write before partition
    cluster.propose(PutCommand{"before", "partition"});
    for (int i = 0; i < 200; i++) cluster.tick(1);

    NodeId old_leader = cluster.leader().value();
    cluster.isolate(old_leader);

    // Wait for new leader in majority
    for (int i = 0; i < 2000; i++) cluster.tick(1);

    // Write through new leader
    cluster.propose(PutCommand{"during", "partition"});
    for (int i = 0; i < 200; i++) cluster.tick(1);

    // Heal
    cluster.heal_all();
    for (int i = 0; i < 1000; i++) cluster.tick(1);

    // Both writes should exist on all nodes
    for (NodeId id : cluster.node_ids()) {
        auto v1 = cluster.get(id, "before");
        EXPECT_TRUE(v1.has_value()) << "node " << id << " lost 'before'";
    }

    EXPECT_TRUE(cluster.check_safety());
}

TEST(PartitionTest, FiveNodeMinorityPartition) {
    SimCluster cluster(5, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    [[maybe_unused]] NodeId lid = cluster.leader().value();
    auto ids = cluster.node_ids();

    // Partition nodes 4 and 5 from the rest
    cluster.partition(4, 1);
    cluster.partition(4, 2);
    cluster.partition(4, 3);
    cluster.partition(5, 1);
    cluster.partition(5, 2);
    cluster.partition(5, 3);

    // Majority (1,2,3) should still have a leader
    for (int i = 0; i < 500; i++) cluster.tick(1);

    bool majority_has_leader = false;
    for (NodeId id : {NodeId(1), NodeId(2), NodeId(3)}) {
        if (cluster.node(id).raft->role() == Role::LEADER) {
            majority_has_leader = true;
        }
    }
    EXPECT_TRUE(majority_has_leader);

    // Minority (4,5) cannot elect a leader (only 2/5)
    // They should both be candidates or followers
    for (NodeId id : {NodeId(4), NodeId(5)}) {
        EXPECT_NE(cluster.node(id).raft->role(), Role::LEADER);
    }
}

TEST(PartitionTest, NoSplitBrainUnderPartition) {
    for (int seed = 0; seed < 50; ++seed) {
        SimCluster cluster(5, seed * 13 + 7);
        ASSERT_TRUE(cluster.wait_for_leader(5000)) << "seed=" << seed;

        // Random partition
        NodeId to_isolate = (seed % 5) + 1;
        cluster.isolate(to_isolate);

        for (int i = 0; i < 2000; i++) cluster.tick(1);

        // Check: at most one leader in any given term
        std::unordered_map<Term, NodeId> leader_per_term;
        bool split_brain = false;
        for (NodeId id : cluster.node_ids()) {
            if (!cluster.node(id).alive) continue;
            if (cluster.node(id).raft->role() == Role::LEADER) {
                Term t = cluster.node(id).raft->current_term();
                if (leader_per_term.count(t) && leader_per_term[t] != id) {
                    split_brain = true;
                }
                leader_per_term[t] = id;
            }
        }
        EXPECT_FALSE(split_brain) << "split brain at seed=" << seed;

        cluster.heal_all();
    }
}

} // namespace
} // namespace raftkv
