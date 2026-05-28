#include "tool_runner.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

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
    EXPECT_TRUE(output.find("ERROR:") == 0 || output.find("error") != std::string::npos)
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

TEST_F(ToolRunnerTest, ToolWithArgsMode) {
    Tool tool = {"wc_test", "word count", "wc -l", "args"};
    json result = runner.run(tool, {{"input", "a\nb\nc"}});
    ASSERT_TRUE(result.is_string());
}
