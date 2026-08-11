#include <gtest/gtest.h>

#include "raftkv/sim_transport.h"

namespace raftkv {
namespace {

// Figure 8 test: The commit-term rule.
// A leader must NOT commit entries from previous terms by counting replicas alone.
// It can only commit an entry from a previous term after committing an entry from
// its current term (which implicitly commits everything before it).
//
// This test creates the scenario from Figure 8 in the Raft paper where a leader
// could incorrectly think a previous-term entry is committed.
TEST(Figure8Test, CommitTermRule) {
    // This is a critical safety test. We verify that the advance_commit_index
    // function only advances to entries whose term == currentTerm.
    //
    // Scenario: Create entries from different terms and verify only current-term
    // entries trigger commit advancement.

    for (int seed = 0; seed < 100; ++seed) {
        SimCluster cluster(5, seed * 37 + 1);
        ASSERT_TRUE(cluster.wait_for_leader(5000)) << "seed=" << seed;

        NodeId leader1 = cluster.leader().value();

        // Leader proposes an entry
        cluster.propose(PutCommand{"key1", "val1"});
        for (int i = 0; i < 100; i++) cluster.tick(1);

        // Isolate the current leader before it can fully commit
        cluster.isolate(leader1);

        // Wait for new leader
        for (int i = 0; i < 2000; i++) cluster.tick(1);

        // New leader proposes (this creates entries in a new term)
        cluster.propose(PutCommand{"key2", "val2"});
        for (int i = 0; i < 200; i++) cluster.tick(1);

        // Heal and let cluster converge
        cluster.heal_all();
        for (int i = 0; i < 2000; i++) cluster.tick(1);

        // Safety invariant must hold: no committed entries can diverge
        EXPECT_TRUE(cluster.check_safety()) << "safety violated at seed=" << seed;
    }
}

TEST(Figure8Test, CommitTermRuleStress) {
    // More aggressive version with multiple leader changes
    for (int seed = 0; seed < 50; ++seed) {
        SimCluster cluster(5, seed * 53 + 11);
        ASSERT_TRUE(cluster.wait_for_leader(5000)) << "seed=" << seed;

        for (int round = 0; round < 5; ++round) {
            // Propose entries
            cluster.propose(PutCommand{"r" + std::to_string(round), std::to_string(round)});
            for (int i = 0; i < 50; i++) cluster.tick(1);

            // Kill current leader
            auto lid = cluster.leader();
            if (lid.has_value()) {
                cluster.isolate(lid.value());
            }

            // Wait for new leader
            for (int i = 0; i < 1500; i++) cluster.tick(1);

            // Heal
            cluster.heal_all();
            for (int i = 0; i < 200; i++) cluster.tick(1);
        }

        // Final convergence
        for (int i = 0; i < 3000; i++) cluster.tick(1);

        EXPECT_TRUE(cluster.check_safety()) << "safety violated at seed=" << seed;
    }
}

TEST(Figure8Test, PreviousTermEntriesCommittedByNewLeader) {
    // Verify that entries from a previous term DO get committed eventually,
    // but only after the new leader commits something in its own term.
    SimCluster cluster(3, 42);
    ASSERT_TRUE(cluster.wait_for_leader(5000));

    // Propose a value
    cluster.propose(PutCommand{"initial", "value"});

    // Wait for full replication
    for (int i = 0; i < 500; i++) cluster.tick(1);

    // Kill leader, new election happens
    NodeId old_leader = cluster.leader().value();
    cluster.kill(old_leader);
    for (int i = 0; i < 2000; i++) cluster.tick(1);

    auto new_leader = cluster.leader();
    ASSERT_TRUE(new_leader.has_value());
    EXPECT_NE(new_leader.value(), old_leader);

    // The new leader's no-op (appended on election) should commit the old entry
    for (int i = 0; i < 500; i++) cluster.tick(1);

    // Verify the old value is present on remaining nodes
    for (NodeId id : cluster.node_ids()) {
        if (id == old_leader) continue;
        auto val = cluster.get(id, "initial");
        EXPECT_TRUE(val.has_value()) << "node " << id << " lost entry from previous leader";
    }
}

} // namespace
} // namespace raftkv
