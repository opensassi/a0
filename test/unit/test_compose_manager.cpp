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

    Skill makeSkill(const std::string& name,
                    const std::string& composeFile = "") {
        Skill s;
        s.name = name;
        s.composeFile = composeFile;
        return s;
    }
};

TEST_F(ComposeManagerTest, StartWithNoComposeFileReturnsEmpty) {
    Skill s = makeSkill("simple");
    std::string network = mgr->startEnvironment(s, "/tmp");
    EXPECT_TRUE(network.empty());
}

TEST_F(ComposeManagerTest, StartEnvironmentReturnsNetworkName) {
    Skill s = makeSkill("db_skill", "docker-compose.yml");
    std::string network = mgr->startEnvironment(s, "/tmp/test_project");
    EXPECT_FALSE(network.empty());
    EXPECT_EQ(network, "test_project_default");
}

TEST_F(ComposeManagerTest, StartEnvironmentIsIdempotent) {
    Skill s = makeSkill("idempotent", "docker-compose.yml");
    std::string net1 = mgr->startEnvironment(s, "/tmp/proj");
    std::string net2 = mgr->startEnvironment(s, "/tmp/proj");
    EXPECT_EQ(net1, net2);
}

TEST_F(ComposeManagerTest, StopEnvironmentDoesNotThrow) {
    Skill s = makeSkill("stoppable", "docker-compose.yml");
    mgr->startEnvironment(s, "/tmp/proj");
    EXPECT_NO_THROW(mgr->stopEnvironment(s));
}

TEST_F(ComposeManagerTest, MarkUsedDoesNotThrow) {
    Skill s = makeSkill("tracked", "docker-compose.yml");
    mgr->startEnvironment(s, "/tmp/proj");
    EXPECT_NO_THROW(mgr->markUsed(s));
}

TEST_F(ComposeManagerTest, NetworkForCurrentSkill) {
    Skill s = makeSkill("current_test", "docker-compose.yml");
    mgr->startEnvironment(s, "/tmp/myapp");
    mgr->setCurrentSkill(s);

    std::string network = mgr->getCurrentNetwork();
    EXPECT_EQ(network, "myapp_default");

    mgr->clearCurrentSkill();
    EXPECT_TRUE(mgr->getCurrentNetwork().empty());
}

TEST_F(ComposeManagerTest, MultipleSkillsDifferentNetworks) {
    Skill s1 = makeSkill("app1", "docker-compose.yml");
    Skill s2 = makeSkill("app2", "other-compose.yml");

    std::string net1 = mgr->startEnvironment(s1, "/projects/app1");
    std::string net2 = mgr->startEnvironment(s2, "/projects/app2");

    EXPECT_EQ(net1, "app1_default");
    EXPECT_EQ(net2, "app2_default");
    EXPECT_NE(net1, net2);
}