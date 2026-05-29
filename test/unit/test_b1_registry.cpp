#include "b1_registry.h"
#include <gtest/gtest.h>

using namespace a0::c2;

// ---------------------------------------------------------------------------
// upsertB1
// ---------------------------------------------------------------------------

TEST(B1RegistryTest, UpsertNewInstance) {
    B1Registry reg;
    int rc = reg.upsertB1(100, "/home/test/proj", "devbox");
    // Stub returns -1
    // Real impl: returns 0, listB1s size = 1
    EXPECT_EQ(rc, 0);
}

TEST(B1RegistryTest, UpsertExistingInstance) {
    B1Registry reg;
    reg.upsertB1(100, "/home/test/proj", "devbox");
    int rc = reg.upsertB1(100, "/home/test/proj2", "devbox2");
    // Real impl: updates fields, returns 0
    EXPECT_EQ(rc, 0);
}

// ---------------------------------------------------------------------------
// removeB1
// ---------------------------------------------------------------------------

TEST(B1RegistryTest, RemoveExisting) {
    B1Registry reg;
    reg.upsertB1(100, "/p", "h");
    int rc = reg.removeB1(100);
    // Stub returns -1
    // Real impl: returns 0
    EXPECT_EQ(rc, 0);
}

TEST(B1RegistryTest, RemoveNonexistent) {
    B1Registry reg;
    int rc = reg.removeB1(999);
    // Real impl: returns -1
    EXPECT_EQ(rc, -1);
}

// ---------------------------------------------------------------------------
// updateAgents
// ---------------------------------------------------------------------------

TEST(B1RegistryTest, UpdateAgentsForKnownB1) {
    B1Registry reg;
    reg.upsertB1(100, "/p", "h");

    std::vector<AgentSummary> agents;
    agents.push_back({50, "sess-1", "running", 1000, 1000});
    int rc = reg.updateAgents(100, agents);
    // Stub returns -1
    // Real impl: returns 0
    EXPECT_EQ(rc, 0);
}

TEST(B1RegistryTest, UpdateAgentsForUnknownB1) {
    B1Registry reg;
    std::vector<AgentSummary> agents;
    int rc = reg.updateAgents(999, agents);
    // Real impl: returns -1
    EXPECT_EQ(rc, -1);
}

// ---------------------------------------------------------------------------
// listB1s
// ---------------------------------------------------------------------------

TEST(B1RegistryTest, ListB1sEmpty) {
    B1Registry reg;
    auto result = reg.listB1s();
    EXPECT_TRUE(result.empty());
}

TEST(B1RegistryTest, ListB1sAfterUpsert) {
    B1Registry reg;
    reg.upsertB1(100, "/p", "h");
    auto result = reg.listB1s();
    // Stub returns empty
    // Real impl: size == 1, first.pid == 100
    EXPECT_EQ(result.size(), 1u);
    if (!result.empty()) {
        EXPECT_EQ(result[0].pid, 100);
    }
}

// ---------------------------------------------------------------------------
// getStats
// ---------------------------------------------------------------------------

TEST(B1RegistryTest, GetStatsEmpty) {
    B1Registry reg;
    int b1s, agents, crashed;
    reg.getStats(b1s, agents, crashed);
    EXPECT_EQ(b1s, 0);
    EXPECT_EQ(agents, 0);
    EXPECT_EQ(crashed, 0);
}

TEST(B1RegistryTest, GetStatsWithAgents) {
    B1Registry reg;
    reg.upsertB1(100, "/p", "h");

    std::vector<AgentSummary> ag;
    ag.push_back({1, "s1", "running", 0, 0});
    ag.push_back({2, "s2", "crashed", 0, 0});
    ag.push_back({3, "s3", "running", 0, 0});
    reg.updateAgents(100, ag);

    int b1s, agents, crashed;
    reg.getStats(b1s, agents, crashed);
    // Stub returns all zeros
    // Real impl: b1s=1, agents=3, crashed=1
    EXPECT_EQ(b1s, 1);
    EXPECT_EQ(agents, 3);
    EXPECT_EQ(crashed, 1);
}

// ---------------------------------------------------------------------------
// pruneStale
// ---------------------------------------------------------------------------

TEST(B1RegistryTest, PruneStaleNoneStale) {
    B1Registry reg;
    reg.upsertB1(100, "/p", "h");
    int pruned = reg.pruneStale(60);
    // Real impl: returns 0 (not stale yet)
    EXPECT_EQ(pruned, 0);
}
