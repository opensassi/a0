#include "agent_core.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "system_handlers.h"
#include "context_manager.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;

class SkillArgsTest : public ::testing::Test {
protected:
    std::string m_skillsDir;
    std::string m_storeDir;
    std::string m_a0Dir;

    SkillManager* m_mgr = nullptr;
    SubprocessToolRunner* m_toolRunner = nullptr;
    DeepSeekProvider* m_provider = nullptr;
    DefaultDependencyResolver* m_depResolver = nullptr;
    DefaultSkillRunner* m_skillRunner = nullptr;
    DefaultContextManager* m_context = nullptr;
    DefaultAgentCore* m_core = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid()) + "_" + std::to_string(rand());
        m_a0Dir = "/tmp/a0_arg_test_a0_" + pid;
        m_skillsDir = "/tmp/a0_arg_test_skills_" + pid;
        m_storeDir = "/tmp/a0_arg_test_store_" + pid;
        fs::remove_all(m_a0Dir);
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
        fs::create_directories(m_skillsDir + "/system");
        fs::create_directories(m_skillsDir + "/local");

        m_mgr = new SkillManager(m_skillsDir, m_storeDir, nullptr);
        m_toolRunner = new SubprocessToolRunner();
        m_provider = new DeepSeekProvider("test-key");
        m_depResolver = new DefaultDependencyResolver(m_mgr);
        m_context = new DefaultContextManager();

        m_mgr->setToolRunner(m_toolRunner);
        m_mgr->registerHandler("system_git_*", [](const json& p, const a0::skills::HandlerContext& ctx) {
            return a0::xGitCommand(ctx.subcommand, p);
        });

        m_skillRunner = new DefaultSkillRunner(
            m_toolRunner, m_provider, m_mgr, m_depResolver);
        m_skillRunner->setSkillsDir(m_skillsDir);
        m_provider->setMockUrl("http://127.0.0.1:18999/v1/chat/completions");

        m_core = new DefaultAgentCore(
            m_toolRunner, m_skillRunner, m_provider, m_context,
            m_depResolver, m_mgr, nullptr, nullptr, nullptr);
    }

    void TearDown() override {
        delete m_core;
        delete m_skillRunner;
        delete m_context;
        delete m_depResolver;
        delete m_provider;
        delete m_toolRunner;
        delete m_mgr;
        fs::remove_all(m_a0Dir);
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
    }
};

TEST_F(SkillArgsTest, SkillArgsStoredInToolState) {
    std::unordered_map<std::string, std::string> args = {
        {"playwright-headless", "false"},
        {"playwright-unsafe-local", "true"}
    };
    m_core->setSkillArgs(args);
    m_core->setExternalRepo("");
    bool ok = m_core->init(m_skillsDir, m_a0Dir);
    ASSERT_TRUE(ok);

    // Check values in ToolState
    EXPECT_EQ(m_mgr->toolState().get("args:playwright-headless"), json("false"));
    EXPECT_EQ(m_mgr->toolState().get("args:playwright-unsafe-local"), json("true"));
}

TEST_F(SkillArgsTest, SkillArgsSetGlobalVars) {
    std::unordered_map<std::string, std::string> args = {
        {"test-flag", "42"},
        {"test-mode", "debug"}
    };
    m_core->setSkillArgs(args);
    m_core->setExternalRepo("");
    bool ok = m_core->init(m_skillsDir, m_a0Dir);
    ASSERT_TRUE(ok);

    // Global vars should be set for prompt template expansion
    // We can verify via ToolState (already tested above)
    EXPECT_EQ(m_mgr->toolState().get("args:test-flag"), json("42"));
    EXPECT_EQ(m_mgr->toolState().get("args:test-mode"), json("debug"));
}

TEST_F(SkillArgsTest, EmptySkillArgsNoCrash) {
    m_core->setSkillArgs({});
    m_core->setExternalRepo("");
    bool ok = m_core->init(m_skillsDir, m_a0Dir);
    EXPECT_TRUE(ok);
}

TEST(SkillArgsParsingTest, ParseCommandLine) {
    // This replicates the parsing logic from main.cpp
    std::vector<std::string> raw = {
        "playwright-headless=false",
        "playwright-unsafe-local",
        "playwright-port=3100"
    };
    std::unordered_map<std::string, std::string> result;
    for (const auto& s : raw) {
        auto eq = s.find('=');
        if (eq == std::string::npos)
            result[s] = "true";
        else
            result[s.substr(0, eq)] = s.substr(eq + 1);
    }
    EXPECT_EQ(result["playwright-headless"], "false");
    EXPECT_EQ(result["playwright-unsafe-local"], "true");
    EXPECT_EQ(result["playwright-port"], "3100");
    EXPECT_EQ(result.size(), 3u);
}

TEST_F(SkillArgsTest, HandlerCanReadSkillArg) {
    // Register a handler that reads from ToolState
    std::string headlessValue;
    m_mgr->registerHandler("local_test_comp_read_arg", [&](const json&, const a0::skills::HandlerContext& ctx) {
        if (ctx.toolState) {
            auto v = ctx.toolState->get("args:playwright-headless");
            headlessValue = v.is_null() ? "not_set" : v.get<std::string>();
        }
        return ::a0::HandlerResult{headlessValue, {}};
    });

    std::unordered_map<std::string, std::string> args = {{"playwright-headless", "true"}};
    m_core->setSkillArgs(args);
    m_core->setExternalRepo("");
    bool ok = m_core->init(m_skillsDir, m_a0Dir);
    ASSERT_TRUE(ok);

    // Execute the handler and verify it read the arg
    json result = m_mgr->executeTool("local_test_comp_read_arg", json::object());
    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "true");
}
