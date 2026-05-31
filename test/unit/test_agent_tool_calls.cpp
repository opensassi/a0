#include "agent_core.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "system_tools.h"
#include "context_manager.h"
#include "schema_inference_engine.h"
#include "persistence/persistence_store.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;

class AgentToolCallTest : public ::testing::Test {
protected:
    std::string m_skillsDir;
    std::string m_storeDir;
    std::string m_pid;

    SkillManager* m_mgr = nullptr;
    a0::SystemToolRegistry* m_systemTools = nullptr;
    SubprocessToolRunner* m_toolRunner = nullptr;
    DeepSeekProvider* m_provider = nullptr;
    DefaultDependencyResolver* m_depResolver = nullptr;
    DefaultSkillRunner* m_skillRunner = nullptr;
    DefaultContextManager* m_context = nullptr;
    a0::persistence::NullStore* m_persistence = nullptr;
    DefaultSchemaInferenceEngine* m_inference = nullptr;
    DefaultAgentCore* m_core = nullptr;

    std::string m_mockUrl;

    void SetUp() override {
        m_pid = std::to_string(::getpid()) + "_" + std::to_string(rand());
        m_skillsDir = "/tmp/a0_tc_skills_" + m_pid;
        m_storeDir = "/tmp/a0_tc_store_" + m_pid;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
        fs::create_directories(m_skillsDir);
        fs::create_directories(m_storeDir);

        m_mgr = new SkillManager(m_skillsDir, m_storeDir, nullptr);
        m_systemTools = new a0::SystemToolRegistry();
        m_toolRunner = new SubprocessToolRunner();
        m_provider = new DeepSeekProvider("test-key");
        m_depResolver = new DefaultDependencyResolver(m_mgr);
        m_context = new DefaultContextManager();
        m_persistence = new a0::persistence::NullStore();
        m_inference = new DefaultSchemaInferenceEngine(m_provider);

        m_skillRunner = new DefaultSkillRunner(
            m_toolRunner, m_provider, m_mgr, m_depResolver, m_systemTools);
        m_skillRunner->setSkillsDir(m_skillsDir);

        m_mockUrl = "http://127.0.0.1:18999/v1/chat/completions";
        m_provider->setMockUrl(m_mockUrl);

        m_core = new DefaultAgentCore(
            m_toolRunner, m_skillRunner, m_provider, m_context,
            m_depResolver, m_inference, m_systemTools, m_mgr,
            m_persistence, nullptr, nullptr);
    }

    void TearDown() override {
        delete m_core;
        delete m_skillRunner;
        delete m_inference;
        delete m_context;
        delete m_depResolver;
        delete m_provider;
        delete m_persistence;
        delete m_toolRunner;
        delete m_systemTools;
        delete m_mgr;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
    }

    void addComponent(const std::string& component, const SkillManifest& manifest) {
        std::string dir = m_skillsDir + "/local/" + component;
        fs::create_directories(dir);
        json j;
        j["name"] = manifest.name;
        j["version"] = manifest.version;
        j["description"] = manifest.description;
        for (const auto& t : manifest.tools) {
            json jt;
            jt["name"] = t.name;
            jt["description"] = t.description;
            jt["command"] = t.command;
            jt["inputMode"] = t.inputMode;
            j["tools"].push_back(jt);
        }
        for (const auto& p : manifest.prompts) {
            json jp;
            jp["name"] = p.name;
            jp["description"] = p.description;
            jp["prompt"] = p.prompt;
            jp["dependencies"] = p.dependencies;
            j["prompts"].push_back(jp);
        }
        std::ofstream f(dir + "/skill.json");
        f << j.dump(2) << "\n";
    }

    void loadSkills() {
        m_mgr->loadAll();
        m_core->init(m_skillsDir);
    }
};

TEST_F(AgentToolCallTest, ProviderToolCallComplete) {
    // Directly test the tool-calling variant of DeepSeekProvider::complete
    std::vector<::ToolSchema> tools;
    ::ToolSchema ts;
    ts.name = "bash";
    ts.description = "Execute bash";
    ts.inputSchema = {{"type", "object"}, {"properties", {{"command", {{"type", "string"}}}}}};
    tools.push_back(ts);

    std::vector<Message> messages;
    messages.push_back({"user", "run a command"});

    // This will fail to connect (non-routable URL) but should not crash
    CompletionResponse resp = m_provider->complete("system prompt", messages, tools);
    // The request will fail or succeed depending on mock server
    // The important thing is no crash, exception safety
    SUCCEED();
}

TEST_F(AgentToolCallTest, ProcessGoalPhase2Dispatch) {
    // Phase 2 is triggered when no exact prompt matches the goal
    // The mock server URL is non-routable, so the HTTP call will fail
    // This exercises the curl setup, headers, payload building for the tool-calling path
    loadSkills();
    
    // No skills match "random goal" → falls to Phase 2
    // Phase 2 builds the dispatch table and tries to call the LLM
    // The LLM call fails (connection refused), returning empty
    json result = m_core->processGoal("random goal with no match");
    // Should not crash
    SUCCEED();
}

TEST_F(AgentToolCallTest, ProcessGoalPhase2WithSystemTools) {
    // Same as above but with system tools loaded
    // This exercises the dispatch table building with system tools
    loadSkills();

    // Add a skill component (prompt only, no matching tool)
    SkillManifest m;
    m.name = "misc";
    m.version = "1.0.0";
    Prompt p;
    p.name = "hello_world";
    p.description = "Hello";
    p.prompt = "hello";
    m.prompts.push_back(p);
    addComponent("misc", m);
    m_mgr->loadAll();

    json result = m_core->processGoal("something unmatched");
    SUCCEED();
}

TEST_F(AgentToolCallTest, RunToolThroughExecutor) {
    // Test that runSkill with a tool-invoking prompt works
    SkillManifest m;
    m.name = "exe_comp";
    m.version = "1.0.0";
    SkillTool tool;
    tool.name = "greeter";
    tool.description = "echoes";
    tool.command = "echo";
    tool.inputMode = "stdin";
    m.tools.push_back(tool);
    Prompt p;
    p.name = "run_greet";
    p.description = "Runs greeter";
    p.prompt = "{{tool:greeter input=\"world\"}}";
    p.dependencies = {"local:exe_comp:greeter"};
    m.prompts.push_back(p);
    addComponent("exe_comp", m);
    loadSkills();

    json result = m_core->runSkill("local:exe_comp:run_greet", json::object());
    EXPECT_FALSE(result.is_null());
}
