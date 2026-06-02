// Tests that exercise virtual destructors in abstract interfaces
#include "agent_interfaces.h"
#include "agent_core.h"
#include "skill_registry.h"
#include "context_manager.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include <chrono>
#include <gtest/gtest.h>

TEST(InterfaceTest, DeleteThroughBasePointer) {
    SkillRegistry* reg = new FileSystemSkillRegistry();
    delete reg;

    ToolRunner* tr = new SubprocessToolRunner();
    delete tr;

    InferenceProvider* ip = new DeepSeekProvider("key");
    delete ip;

    ContextManager* cm = new DefaultContextManager();
    delete cm;

    DependencyResolver* dr = new DefaultDependencyResolver(
        new FileSystemSkillRegistry());
    delete dr;

    SkillRegistry* r2 = new FileSystemSkillRegistry();
    DependencyResolver* dr2 = new DefaultDependencyResolver(r2);
    SkillRunner* sr = new DefaultSkillRunner(
        new SubprocessToolRunner(), new DeepSeekProvider("k"), r2, dr2);
    delete sr;
    delete dr2;
    delete r2;

}

TEST(InterfaceTest, VirtualDestructors) {
    FileSystemSkillRegistry reg;
    SubprocessToolRunner tr;
    DeepSeekProvider ip("test");
    DefaultContextManager cm;
    FileSystemSkillRegistry reg2;
    DefaultDependencyResolver dr(&reg2);
    DefaultSkillRunner sr(&tr, &ip, &reg2, &dr);
}

TEST(InterfaceTest, SkillRunnerThroughInterface) {
    FileSystemSkillRegistry reg;
    SubprocessToolRunner tr;
    DeepSeekProvider ip("key");
    DefaultDependencyResolver dr(&reg);
    DefaultSkillRunner* sr = new DefaultSkillRunner(&tr, &ip, &reg, &dr);
    SkillRunner* iface = sr;
    EXPECT_NE(iface, nullptr);
    delete iface;
}

TEST(InterfaceTest, ToolRunnerArgsModeContract) {
    SubprocessToolRunner runner;
    Tool tool{"echo_test", "echo", "sh -c 'echo $1' _", "args"};
    json params = {{"_", "hello_args"}};
    json result = runner.run(tool, params);
    EXPECT_TRUE(result.is_string());
    std::string output = result.get<std::string>();
    EXPECT_EQ(output, "hello_args\n");
}

TEST(InterfaceTest, ToolRunnerTimeoutContract) {
    SubprocessToolRunner runner;
    Tool tool{"sleep_test", "sleep", "sleep 31", "stdin"};
    auto start = std::chrono::steady_clock::now();
    json result = runner.run(tool, json::object());
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start).count();
    ASSERT_TRUE(result.is_string());
    EXPECT_TRUE(result.get<std::string>().find("ERROR: timeout") == 0);
    EXPECT_LE(elapsed, 32);
}

TEST(InterfaceTest, SkillRunnerParameterSubstitution) {
    FileSystemSkillRegistry reg;
    SubprocessToolRunner tr;
    DeepSeekProvider ip("key");
    DefaultDependencyResolver dr(&reg);
    DefaultSkillRunner sr(&tr, &ip, &reg, &dr);
    Prompt p{"test", "", "Goal: {{goal}}", {}, {}};
    json params = {{"goal", "hello"}};
    std::string expanded = sr.expandPrompt(p, params);
    EXPECT_NE(expanded.find("hello"), std::string::npos)
        << "Parameter substitution not implemented";
}
