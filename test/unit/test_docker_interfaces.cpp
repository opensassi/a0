#include "agent_interfaces.h"
#include "docker/container_manager.h"
#include "docker/compose_manager.h"
#include "docker/docker_tool_runner.h"
#include <gtest/gtest.h>

TEST(DockerInterfaceTest, DockerToolRunnerThroughInterface) {
    a0::docker::DockerContainerManager cm(300, 10, "ubuntu:22.04");
    a0::docker::DockerComposeManager compMgr(300);
    a0::docker::DockerToolRunnerImpl runner(&cm, &compMgr);
    ToolRunner* iface = &runner;
    EXPECT_NE(iface, nullptr);
}

TEST(DockerInterfaceTest, ContainerManagerThroughInterface) {
    a0::docker::DockerContainerManager mgr(300, 10, "ubuntu:22.04");
    ContainerManager* iface = &mgr;
    EXPECT_NE(iface, nullptr);
}

TEST(DockerInterfaceTest, ComposeManagerThroughInterface) {
    a0::docker::DockerComposeManager mgr(300);
    ComposeManager* iface = &mgr;
    EXPECT_NE(iface, nullptr);
}

TEST(DockerInterfaceTest, TrustLevelValues) {
    EXPECT_NE(static_cast<int>(TrustLevel::HIGH),
              static_cast<int>(TrustLevel::LOW));
    EXPECT_EQ(static_cast<int>(TrustLevel::MEDIUM), 1);
}

TEST(DockerInterfaceTest, ToolDockerFieldsDefault) {
    Tool t;
    EXPECT_TRUE(t.dockerImage.empty());
    EXPECT_EQ(t.trustLevel, TrustLevel::MEDIUM);
    EXPECT_TRUE(t.aptDependencies.empty());
}

TEST(DockerInterfaceTest, SkillDockerFieldsDefault) {
    Prompt p;
    EXPECT_TRUE(p.composeFile.empty());
    EXPECT_TRUE(p.aptDependencies.empty());
}