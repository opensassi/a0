#include "executor/tool_runner.h"
#include <chrono>
#include <csignal>
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>
#include <thread>

// Ignore SIGPIPE globally — subprocess pipes may close during parallel test
// execution when the parent exits before the child finishes writing.
static struct SigPipeGuard { SigPipeGuard() { signal(SIGPIPE, SIG_IGN); } } g_sigPipeGuard;

class ToolRunnerTest : public ::testing::Test {
protected:
    SubprocessToolRunner runner;

    Tool makeBashTool() {
        return {"bash", "Execute bash command", "bash", "stdin"};
    }

    Tool makeEchoTool() {
        return {"echo_test", "Echo input", "echo", "stdin"};
    }
};

TEST_F(ToolRunnerTest, BashEchoHello) {
    Tool tool = makeBashTool();
    json result = runner.run(tool, {{"input", "echo hello"}});
    ASSERT_TRUE(result.is_string());
    std::string output = result.get<std::string>();
    EXPECT_EQ(output, "hello\n");
}

TEST_F(ToolRunnerTest, StdinInputPassedThrough) {
    Tool tool = {"cat_test", "Pass stdin through", "cat", "stdin"};
    json result = runner.run(tool, {{"input", "hello world"}});
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "hello world");
}

TEST_F(ToolRunnerTest, CommandNotFound) {
    Tool tool = {"nonexistent", "Does not exist", "nonexistent_cmd_xyz", "stdin"};
    json result = runner.run(tool, json::object());
    ASSERT_TRUE(result.is_string());
    std::string output = result.get<std::string>();
    EXPECT_TRUE(output.find("not found") != std::string::npos)
        << "Got: " << output;
}

TEST_F(ToolRunnerTest, EmptyParams) {
    Tool tool = makeEchoTool();
    json result = runner.run(tool, json::object());
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, LongOutputTruncation) {
    std::string bigData(1024 * 1024 + 100, 'A');
    Tool tool = makeBashTool();
    json result = runner.run(tool, {{"input", "echo '" + bigData + "'"}});
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, ParamsPassedViaJson) {
    Tool tool = makeBashTool();
    json params = {{"input", "echo '{\"key\":\"value\"}'"}};
    json result = runner.run(tool, params);
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, NonObjectParams) {
    Tool tool = makeBashTool();
    json result = runner.run(tool, json("echo 'direct string'"));
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, ArgsModeWithPositionalArg) {
    Tool tool = {"echo_arg", "echo first arg", "sh -c 'echo $1' _", "args"};
    json params = {{"_", "hello"}};
    json result = runner.run(tool, params);
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "hello\n");
}

TEST_F(ToolRunnerTest, ArgsModeWithNumericArg) {
    Tool tool = {"echo_count", "echo count", "sh -c 'echo $1' _", "args"};
    json params = {{"_", 42}};
    json result = runner.run(tool, params);
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "42\n");
}

TEST_F(ToolRunnerTest, ArgsModeWithBooleanArg) {
    Tool tool = {"echo_bool", "echo bool", "sh -c 'echo $1' _", "args"};
    json params = {{"_", true}};
    json result = runner.run(tool, params);
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "true\n");
}

TEST_F(ToolRunnerTest, ArgsModeWithStringParam) {
    Tool tool = {"echo_string", "echo string param", "sh -c 'echo $1' _", "args"};
    json result = runner.run(tool, json("direct_string"));
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, ArgsModeWithNamedArgs) {
    Tool tool = {"echo_named", "echo named args", "sh -c 'echo \"$@\"' _", "args"};
    json params = {{"file", "test.txt"}, {"verbose", "true"}};
    json result = runner.run(tool, params);
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "--file=test.txt --verbose=true\n");
}

TEST_F(ToolRunnerTest, ToolRunnerRunStreaming) {
    Tool tool = makeBashTool();
    std::string accumulated;
    a0::StreamHandle handle = runner.runStreaming(tool, {{"input", "echo streaming_test"}},
        [&](const std::string& data, const std::string& dir) {
            accumulated += data;
        });
    int rc = handle.wait();
    EXPECT_EQ(rc, 0);
    EXPECT_NE(accumulated.find("streaming_test"), std::string::npos);
}

TEST_F(ToolRunnerTest, TimeoutEnforced) {
    Tool tool = makeBashTool();
    tool.timeoutSecs = 2;
    auto start = std::chrono::steady_clock::now();
    json result = runner.run(tool, {{"input", "sleep 10"}});
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();
    ASSERT_TRUE(result.is_string());
    std::string output = result.get<std::string>();
    EXPECT_TRUE(output.find("ERROR: timeout") == 0);
    EXPECT_LE(elapsed, 5);
}

TEST_F(ToolRunnerTest, StdinModeObjectNoInputKey) {
    Tool tool = makeEchoTool();
    json result = runner.run(tool, json::object({{"extra", "data"}}));
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, ArgsModeWithNamedNumberValue) {
    Tool tool = {"echo_count", "echo count", "sh -c 'echo \"$@\"' _", "args"};
    json params = {{"count", 42}, {"verbose", true}};
    json result = runner.run(tool, params);
    ASSERT_TRUE(result.is_string());
    std::string out = result.get<std::string>();
    EXPECT_TRUE(out.find("--count=42") != std::string::npos) << "Got: " << out;
    EXPECT_TRUE(out.find("--verbose=true") != std::string::npos) << "Got: " << out;
}

TEST_F(ToolRunnerTest, ArgsModeJsonArrayValue) {
    Tool tool = {"echo_json", "echo json", "sh -c 'echo \"$@\"' _", "args"};
    json params = {{"items", json::array({1, 2, 3})}};
    json result = runner.run(tool, params);
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, NonObjectParamsStdinMode) {
    Tool tool = makeBashTool();
    json result = runner.run(tool, json(42));
    ASSERT_TRUE(result.is_string());
}

TEST_F(ToolRunnerTest, RunStreamingEmptyInput) {
    Tool tool = makeBashTool();
    std::string accumulated;
    a0::StreamHandle handle = runner.runStreaming(tool, json::object(),
        [&](const std::string& data, const std::string& dir) {
            accumulated += data;
        });
    handle.wait();
}

TEST_F(ToolRunnerTest, TruncationHardLimit) {
    std::string bigData(1024 * 1024 + 500, 'B');
    Tool tool = makeBashTool();
    json result = runner.run(tool, {{"input", "echo '" + bigData + "'"}});
    ASSERT_TRUE(result.is_string());
    std::string out = result.get<std::string>();
    EXPECT_LE(out.size(), 1024 * 1024 + 100);
}
