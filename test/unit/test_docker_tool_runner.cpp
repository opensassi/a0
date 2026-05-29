#include "docker/container_manager.h"
#include "docker/compose_manager.h"
#include "docker/docker_tool_runner.h"
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

class DockerToolRunnerTest : public ::testing::Test {
protected:
    a0::docker::DockerContainerManager* cm;
    a0::docker::DockerComposeManager* compMgr;
    a0::docker::DockerToolRunnerImpl* runner;

    void SetUp() override {
        setupMockPath();
        cm = new a0::docker::DockerContainerManager(3600, 10, "ubuntu:22.04");
        compMgr = new a0::docker::DockerComposeManager(300);
        runner = new a0::docker::DockerToolRunnerImpl(cm, compMgr);
    }

    void TearDown() override {
        delete runner;
        delete compMgr;
        delete cm;
    }

    Tool makeDockerTool(const std::string& name,
                         const std::string& command = "echo hello",
                         const std::string& inputMode = "stdin") {
        Tool t;
        t.name = name;
        t.command = command;
        t.inputMode = inputMode;
        t.dockerImage = "ubuntu:22.04";
        return t;
    }
};

TEST_F(DockerToolRunnerTest, RunStdInMode) {
    Tool t = makeDockerTool("echo_test", "cat");
    json params = {{"input", "hello from docker"}};
    json result = runner->run(t, params);
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "hello from docker");
}

TEST_F(DockerToolRunnerTest, RunWithStringParams) {
    Tool t = makeDockerTool("echo_test", "cat");
    json result = runner->run(t, json("direct string"));
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "direct string");
}

TEST_F(DockerToolRunnerTest, RunArgsModeWithPositional) {
    Tool t = makeDockerTool("args_test",
                             "sh -c 'echo $1' _",
                             "args");
    json params = {{"_", "hello_args"}};
    json result = runner->run(t, params);
    ASSERT_TRUE(result.is_string());
    std::string output = result.get<std::string>();
    // echo $1 appends a newline
    EXPECT_TRUE(output == "hello_args" || output == "hello_args\n");
}

TEST_F(DockerToolRunnerTest, RunArgsModeWithNamed) {
    Tool t = makeDockerTool("named_test",
                             "sh -c 'echo \"$@\"' _",
                             "args");
    json params = {{"file", "test.txt"}, {"verbose", "true"}};
    json result = runner->run(t, params);
    ASSERT_TRUE(result.is_string());
    std::string output = result.get<std::string>();
    EXPECT_NE(output.find("--file=test.txt"), std::string::npos);
    EXPECT_NE(output.find("--verbose=true"), std::string::npos);
}

TEST_F(DockerToolRunnerTest, RunEphemeralMode) {
    Tool t = makeDockerTool("ephem_test", "echo ephemeral", "stdin");
    a0::docker::DockerToolRunnerImpl ephemRunner(cm, compMgr, false);
    json result = ephemRunner.run(t, json::object());
    ASSERT_TRUE(result.is_string());
}

TEST_F(DockerToolRunnerTest, RunWithComposeNetwork) {
    // Simulate a compose environment
    Prompt prompt;
    prompt.name = "compose_skill";
    prompt.composeFile = "docker-compose.yml";

    compMgr->startEnvironment(prompt, "/tmp/compose_proj");
    compMgr->setCurrentPrompt(prompt);

    Tool t = makeDockerTool("net_test", "echo networked");
    json result = runner->run(t, json::object());
    ASSERT_TRUE(result.is_string());

    compMgr->clearCurrentPrompt();
    compMgr->stopEnvironment(prompt);
}

TEST_F(DockerToolRunnerTest, RunWithObjectParams) {
    Tool t = makeDockerTool("obj_test", "cat");
    json params = {{"input", "{\"key\":\"value\"}"}};
    json result = runner->run(t, params);
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "{\"key\":\"value\"}");
}

TEST_F(DockerToolRunnerTest, RunEmptyParams) {
    Tool t = makeDockerTool("empty_test", "echo ok");
    json result = runner->run(t, json::object());
    ASSERT_TRUE(result.is_string());
}