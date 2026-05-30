#include "docker/container_manager.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <string>

static void setupMockPath() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* oldPath = std::getenv("PATH");
    std::string path = std::string(TEST_UNIT_DIR);
    if (oldPath) {
        path += ":" + std::string(oldPath);
    }
    setenv("PATH", path.c_str(), 1);
}

class ContainerManagerTest : public ::testing::Test {
protected:
    a0::docker::DockerContainerManager* mgr;

    void SetUp() override {
        setupMockPath();
        mgr = new a0::docker::DockerContainerManager(3600, 10, "ubuntu:22.04");
    }

    void TearDown() override {
        delete mgr;
    }

    Tool makeTool(const std::string& name,
                  TrustLevel trust = TrustLevel::MEDIUM) {
        Tool t;
        t.name = name;
        t.command = "echo hello";
        t.dockerImage = "ubuntu:22.04";
        t.trustLevel = trust;
        return t;
    }
};

TEST_F(ContainerManagerTest, AcquireReturnsNonEmpty) {
    Tool t = makeTool("test_tool");
    std::string id = mgr->acquireContainer(t);
    EXPECT_FALSE(id.empty());
}

TEST_F(ContainerManagerTest, HighTrustReturnsSameId) {
    Tool t1 = makeTool("tool_a", TrustLevel::HIGH);
    Tool t2 = makeTool("tool_b", TrustLevel::HIGH);

    std::string id1 = mgr->acquireContainer(t1);
    std::string id2 = mgr->acquireContainer(t2);
    EXPECT_EQ(id1, id2);
}

TEST_F(ContainerManagerTest, LowTrustReturnsDifferentIds) {
    Tool t1 = makeTool("tool_a", TrustLevel::LOW);
    t1.dockerImage = "ubuntu:22.04";
    Tool t2 = makeTool("tool_b", TrustLevel::LOW);
    t2.dockerImage = "ubuntu:22.04";

    std::string id1 = mgr->acquireContainer(t1);
    std::string id2 = mgr->acquireContainer(t2);
    EXPECT_NE(id1, id2);
}

TEST_F(ContainerManagerTest, ExecInContainerReturnsOutput) {
    Tool t = makeTool("exec_tool");
    std::string id = mgr->acquireContainer(t);
    std::string output = mgr->execInContainer(id, "echo hello");
    EXPECT_TRUE(output.find("hello") != std::string::npos);
}

TEST_F(ContainerManagerTest, ExecInContainerWithStdin) {
    Tool t = makeTool("stdin_tool");
    std::string id = mgr->acquireContainer(t);
    std::string output = mgr->execInContainer(id, "cat", "test data");
    EXPECT_EQ(output, "test data");
}

TEST_F(ContainerManagerTest, PruneIdleRemovesExpired) {
    a0::docker::DockerContainerManager shortMgr(0, 10, "ubuntu:22.04");
    Tool t = makeTool("prune_tool");
    std::string id = shortMgr.acquireContainer(t);
    EXPECT_FALSE(id.empty());

    // Prune triggers; container removed from pool
    shortMgr.pruneIdleContainers();

    // Should be able to acquire again without error
    std::string id2 = shortMgr.acquireContainer(t);
    EXPECT_FALSE(id2.empty());
}

TEST_F(ContainerManagerTest, ExecInContainerTimeout) {
    Tool t = makeTool("timeout_tool");
    t.timeoutSecs = 2;
    std::string id = mgr->acquireContainer(t);
    std::string output = mgr->execInContainer(id, "sleep 10", "", t.timeoutSecs);
    EXPECT_TRUE(output.find("ERROR: timeout") == 0 ||
                output.find("ERROR:") == 0);
}

TEST_F(ContainerManagerTest, MediumTrustSamePool) {
    Tool t1 = makeTool("med_a", TrustLevel::MEDIUM);
    Tool t2 = makeTool("med_b", TrustLevel::MEDIUM);

    std::string id1 = mgr->acquireContainer(t1);
    std::string id2 = mgr->acquireContainer(t2);
    EXPECT_EQ(id1, id2);
}