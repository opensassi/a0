// Tests that exercise virtual destructors in abstract interfaces
#include "agent_interfaces.h"
#include "agent_core.h"
#include "component_registry.h"
#include "context_manager.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "invocation_logger.h"
#include "schema_inference_engine.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include <chrono>
#include <gtest/gtest.h>

TEST(InterfaceTest, DeleteThroughBasePointer) {
    ComponentRegistry* reg = new FileSystemComponentRegistry();
    delete reg;

    ToolRunner* tr = new SubprocessToolRunner();
    delete tr;

    InferenceProvider* ip = new DeepSeekProvider("key");
    delete ip;

    ContextManager* cm = new DefaultContextManager();
    delete cm;

    InvocationLogger* il = new JsonLinesLogger("/tmp");
    delete il;

    DependencyResolver* dr = new DefaultDependencyResolver(
        new FileSystemComponentRegistry());
    delete dr;

    ComponentRegistry* r2 = new FileSystemComponentRegistry();
    SkillRunner* sr = new DefaultSkillRunner(
        new SubprocessToolRunner(), new DeepSeekProvider("k"), r2);
    delete sr;
    delete r2;

    InferenceProvider* ip2 = new DeepSeekProvider("k2");
    SchemaInferenceEngine* sie = new DefaultSchemaInferenceEngine(ip2);
    delete sie;
    delete ip2;
}

TEST(InterfaceTest, VirtualDestructors) {
    // Just instantiate and destroy through concrete types
    // to verify vtable linkage is correct
    FileSystemComponentRegistry reg;
    SubprocessToolRunner tr;
    DeepSeekProvider ip("test");
    DefaultContextManager cm;
    JsonLinesLogger il("/tmp/logs");
    FileSystemComponentRegistry reg2;
    DefaultDependencyResolver dr(&reg2);
    DefaultSchemaInferenceEngine sie(&ip);
    DefaultSkillRunner sr(&tr, &ip, &reg2);
}

TEST(InterfaceTest, SkillRunnerThroughInterface) {
    FileSystemComponentRegistry reg;
    SubprocessToolRunner tr;
    DeepSeekProvider ip("key");
    DefaultSkillRunner* sr = new DefaultSkillRunner(&tr, &ip, &reg);
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
    FileSystemComponentRegistry reg;
    SubprocessToolRunner tr;
    DeepSeekProvider ip("key");
    DefaultSkillRunner sr(&tr, &ip, &reg);
    Skill s{"test", "", "Goal: {{goal}}", {}, {}};
    json params = {{"goal", "hello"}};
    std::string expanded = sr.expandPrompt(s, params);
    EXPECT_NE(expanded.find("hello"), std::string::npos)
        << "Parameter substitution not implemented";
}
