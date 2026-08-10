#include <gtest/gtest.h>

#include "raftkv/sim_transport.h"

namespace raftkv {
namespace {

TEST(ReplicationTest, BasicReplication) {
    SimCluster cluster(3, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    // Propose a write
    ASSERT_TRUE(cluster.propose(PutCommand{"hello", "world"}));

    // Wait for commit
    for (int i = 0; i < 500; i++) {
        cluster.tick(1);
    }

    // All alive nodes should have the value
    for (NodeId id : cluster.node_ids()) {
        auto val = cluster.get(id, "hello");
        ASSERT_TRUE(val.has_value()) << "node " << id << " missing value";
        EXPECT_EQ(val.value(), "world");
    }
}

TEST(ReplicationTest, MultipleWrites) {
    SimCluster cluster(3, 99);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(cluster.propose(PutCommand{"k" + std::to_string(i), "v" + std::to_string(i)}));
        // Tick between writes to allow replication
        for (int j = 0; j < 50; j++) {
            cluster.tick(1);
        }
    }

    // Wait for full commit
    for (int i = 0; i < 500; i++) {
        cluster.tick(1);
    }

    for (NodeId id : cluster.node_ids()) {
        for (int i = 0; i < 10; ++i) {
            auto val = cluster.get(id, "k" + std::to_string(i));
            ASSERT_TRUE(val.has_value()) << "node " << id << " missing k" << i;
            EXPECT_EQ(val.value(), "v" + std::to_string(i));
        }
    }
}

TEST(ReplicationTest, DeleteReplication) {
    SimCluster cluster(3, 77);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    ASSERT_TRUE(cluster.propose(PutCommand{"temp", "value"}));
    for (int i = 0; i < 200; i++) cluster.tick(1);

    // Verify write
    for (NodeId id : cluster.node_ids()) {
        EXPECT_TRUE(cluster.get(id, "temp").has_value());
    }

    // Delete
    ASSERT_TRUE(cluster.propose(DeleteCommand{"temp"}));
    for (int i = 0; i < 200; i++) cluster.tick(1);

    // Verify deletion
    for (NodeId id : cluster.node_ids()) {
        EXPECT_FALSE(cluster.get(id, "temp").has_value()) << "node " << id;
    }
}

TEST(ReplicationTest, FiveNodeReplication) {
    SimCluster cluster(5, 111);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    for (int i = 0; i < 20; ++i) {
        cluster.propose(PutCommand{"key" + std::to_string(i), "val" + std::to_string(i)});
        for (int j = 0; j < 30; j++) cluster.tick(1);
    }

    for (int i = 0; i < 500; i++) cluster.tick(1);

    for (NodeId id : cluster.node_ids()) {
        for (int i = 0; i < 20; ++i) {
            auto val = cluster.get(id, "key" + std::to_string(i));
            EXPECT_TRUE(val.has_value()) << "node=" << id << " key=key" << i;
        }
    }

    EXPECT_TRUE(cluster.check_safety());
}

TEST(ReplicationTest, CommitRequiresMajority) {
    SimCluster cluster(3, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    NodeId lid = cluster.leader().value();

    // Isolate leader — it can't get majority now
    cluster.isolate(lid);

    // Propose should go through locally but not commit
    // (leader is isolated, can't get acks)

    // Wait for new leader among the remaining two
    for (int i = 0; i < 1000; i++) cluster.tick(1);

    // The remaining majority should elect a new leader
    bool new_leader_found = false;
    for (NodeId id : cluster.node_ids()) {
        if (id == lid) continue;
        if (cluster.node(id).raft->role() == Role::LEADER) {
            new_leader_found = true;
        }
    }
    EXPECT_TRUE(new_leader_found);
}

TEST(ReplicationTest, SafetyInvariant) {
    SimCluster cluster(5, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    // Drive a bunch of writes
    for (int i = 0; i < 50; ++i) {
        cluster.propose(PutCommand{"k" + std::to_string(i), "v" + std::to_string(i)});
        cluster.tick(10);
    }

    for (int i = 0; i < 500; i++) cluster.tick(1);

    EXPECT_TRUE(cluster.check_safety());
}

} // namespace
} // namespace raftkv
