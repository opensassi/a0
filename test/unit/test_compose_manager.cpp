#include "docker/compose_manager.h"
#include <gtest/gtest.h>
#include <cstdlib>
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

class ComposeManagerTest : public ::testing::Test {
protected:
    a0::docker::DockerComposeManager* mgr;

    void SetUp() override {
        setupMockPath();
        mgr = new a0::docker::DockerComposeManager(300);
    }

    void TearDown() override {
        delete mgr;
    }

    Prompt makePrompt(const std::string& name,
                    const std::string& composeFile = "") {
        Prompt p;
        p.name = name;
        p.composeFile = composeFile;
        return p;
    }
};

TEST_F(ComposeManagerTest, StartWithNoComposeFileReturnsEmpty) {
    Prompt p = makePrompt("simple");
    std::string network = mgr->startEnvironment(p, "/tmp");
    EXPECT_TRUE(network.empty());
}

TEST_F(ComposeManagerTest, StartEnvironmentReturnsNetworkName) {
    Prompt p = makePrompt("db_skill", "docker-compose.yml");
    std::string network = mgr->startEnvironment(p, "/tmp/test_project");
    EXPECT_FALSE(network.empty());
    EXPECT_EQ(network, "test_project_default");
}

TEST_F(ComposeManagerTest, StartEnvironmentIsIdempotent) {
    Prompt p = makePrompt("idempotent", "docker-compose.yml");
    std::string net1 = mgr->startEnvironment(p, "/tmp/proj");
    std::string net2 = mgr->startEnvironment(p, "/tmp/proj");
    EXPECT_EQ(net1, net2);
}

TEST_F(ComposeManagerTest, StopEnvironmentDoesNotThrow) {
    Prompt p = makePrompt("stoppable", "docker-compose.yml");
    mgr->startEnvironment(p, "/tmp/proj");
    EXPECT_NO_THROW(mgr->stopEnvironment(p));
}

TEST_F(ComposeManagerTest, MarkUsedDoesNotThrow) {
    Prompt p = makePrompt("tracked", "docker-compose.yml");
    mgr->startEnvironment(p, "/tmp/proj");
    EXPECT_NO_THROW(mgr->markUsed(p));
}

TEST_F(ComposeManagerTest, NetworkForCurrentPrompt) {
    Prompt p = makePrompt("current_test", "docker-compose.yml");
    mgr->startEnvironment(p, "/tmp/myapp");
    mgr->setCurrentPrompt(p);

    std::string network = mgr->getCurrentNetwork();
    EXPECT_EQ(network, "myapp_default");

    mgr->clearCurrentPrompt();
    EXPECT_TRUE(mgr->getCurrentNetwork().empty());
}

TEST_F(ComposeManagerTest, MultipleSkillsDifferentNetworks) {
    Prompt p1 = makePrompt("app1", "docker-compose.yml");
    Prompt p2 = makePrompt("app2", "other-compose.yml");

    std::string net1 = mgr->startEnvironment(p1, "/projects/app1");
    std::string net2 = mgr->startEnvironment(p2, "/projects/app2");

    EXPECT_EQ(net1, "app1_default");
    EXPECT_EQ(net2, "app2_default");
    EXPECT_NE(net1, net2);
}

// ===========================================================================
// Persistent mode
// ===========================================================================

TEST_F(ComposeManagerTest, StartPersistent_ReturnsNetwork) {
    std::string network = mgr->startPersistent("persist_test", "docker-compose.yml", "/tmp/proj");
    EXPECT_FALSE(network.empty());
    EXPECT_EQ(network, "proj_default");
}

TEST_F(ComposeManagerTest, StartPersistent_MarksAsPersistent) {
    mgr->startPersistent("persistent_skill", "docker-compose.yml", "/tmp/app");
    EXPECT_TRUE(mgr->isPersistent("persistent_skill"));
}

TEST_F(ComposeManagerTest, TransientNotPersistent) {
    Prompt p = makePrompt("transient", "docker-compose.yml");
    mgr->startEnvironment(p, "/tmp/proj");
    EXPECT_FALSE(mgr->isPersistent("transient"));
}

TEST_F(ComposeManagerTest, StopPersistent_ClearsFlag) {
    mgr->startPersistent("will_stop", "docker-compose.yml", "/tmp/app");
    EXPECT_TRUE(mgr->isPersistent("will_stop"));
    mgr->stopPersistent("will_stop");
    EXPECT_FALSE(mgr->isPersistent("will_stop"));
}

TEST_F(ComposeManagerTest, StopPersistent_UnknownName_NoThrow) {
    EXPECT_NO_THROW(mgr->stopPersistent("does_not_exist"));
}

TEST_F(ComposeManagerTest, StartPersistent_EmptyComposeFile_ReturnsEmpty) {
    std::string network = mgr->startPersistent("empty", "", "/tmp");
    EXPECT_TRUE(network.empty());
    EXPECT_FALSE(mgr->isPersistent("empty"));
}

TEST_F(ComposeManagerTest, TransientStop_DoesNotAffectPersistent) {
    // Start transient
    Prompt p = makePrompt("transient_app", "docker-compose.yml");
    mgr->startEnvironment(p, "/tmp/proj");
    EXPECT_FALSE(mgr->isPersistent("transient_app"));

    // Start persistent with different name
    mgr->startPersistent("persistent_app", "docker-compose.yml", "/tmp/proj");
    EXPECT_TRUE(mgr->isPersistent("persistent_app"));

    // Stop transient does not affect persistent
    mgr->stopEnvironment(p);
    EXPECT_TRUE(mgr->isPersistent("persistent_app")) << "Stopping transient should not affect persistent stack";
}