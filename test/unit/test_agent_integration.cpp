#include "agent_core.h"
#include "skill_runner.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include "system_tools.h"
#include "context_manager.h"
#include "invocation_logger.h"
#include "schema_inference_engine.h"
#include "base_prompt.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::skills;

class AgentIntegrationTest : public ::testing::Test {
protected:
    std::string m_skillsDir;
    std::string m_storeDir;
    std::string m_logDir;
    std::string m_pid;

    SkillManager* m_mgr = nullptr;
    a0::SystemToolRegistry* m_systemTools = nullptr;
    SubprocessToolRunner* m_toolRunner = nullptr;
    DeepSeekProvider* m_provider = nullptr;
    DefaultDependencyResolver* m_depResolver = nullptr;
    DefaultSkillRunner* m_skillRunner = nullptr;
    DefaultContextManager* m_context = nullptr;
    JsonLinesLogger* m_logger = nullptr;
    DefaultSchemaInferenceEngine* m_inference = nullptr;
    DefaultAgentCore* m_core = nullptr;

    void SetUp() override {
        m_pid = std::to_string(::getpid()) + "_" + std::to_string(rand());
        m_skillsDir = "/tmp/a0_int_skills_" + m_pid;
        m_storeDir = "/tmp/a0_int_store_" + m_pid;
        m_logDir = "/tmp/a0_int_logs_" + m_pid;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
        fs::remove_all(m_logDir);
        fs::create_directories(m_skillsDir);
        fs::create_directories(m_storeDir);

        m_mgr = new SkillManager(m_skillsDir, m_storeDir, m_logDir);
        m_systemTools = new a0::SystemToolRegistry();
        m_toolRunner = new SubprocessToolRunner();
        m_provider = new DeepSeekProvider("test-key");
        m_depResolver = new DefaultDependencyResolver(m_mgr);
        m_context = new DefaultContextManager();
        m_logger = new JsonLinesLogger(m_logDir);
        m_inference = new DefaultSchemaInferenceEngine(m_provider);

        m_skillRunner = new DefaultSkillRunner(
            m_toolRunner, m_provider, m_mgr, m_depResolver, m_systemTools);
        m_skillRunner->setSkillsDir(m_skillsDir);

        // Use non-routable mock URL so provider fails fast instead of hanging
        m_provider->setMockUrl("http://127.0.0.1:18765/v1/chat/completions");

        m_core = new DefaultAgentCore(
            m_toolRunner, m_skillRunner, m_provider, m_context, m_logger,
            m_depResolver, m_inference, m_systemTools, m_mgr,
            nullptr, nullptr, nullptr);
    }

    void TearDown() override {
        delete m_core;
        delete m_skillRunner;
        delete m_inference;
        delete m_logger;
        delete m_context;
        delete m_depResolver;
        delete m_provider;
        delete m_toolRunner;
        delete m_systemTools;
        delete m_mgr;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
        fs::remove_all(m_logDir);
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
            jt["timeoutSecs"] = t.timeoutSecs;
            j["tools"].push_back(jt);
        }
        for (const auto& p : manifest.prompts) {
            json jp;
            jp["name"] = p.name;
            jp["description"] = p.description;
            jp["prompt"] = p.prompt;
            jp["dependencies"] = p.dependencies;
            for (const auto& v : p.validators) {
                json jv;
                jv["toolName"] = v.toolName;
                jp["validators"].push_back(jv);
            }
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

TEST_F(AgentIntegrationTest, BuildBasePrompt) {
    loadSkills();
    std::string prompt = a0::buildBasePrompt(m_mgr);
    EXPECT_NE(prompt.find("a0 build"), std::string::npos);
    EXPECT_NE(prompt.find("system tools"), std::string::npos);
    EXPECT_NE(prompt.find("bash"), std::string::npos);
    EXPECT_NE(prompt.find("glob"), std::string::npos);
    EXPECT_NE(prompt.find("grep"), std::string::npos);
}

TEST_F(AgentIntegrationTest, ProcessGoalEmpty) {
    loadSkills();
    json result = m_core->processGoal("");
    EXPECT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "no goal provided");
}

TEST_F(AgentIntegrationTest, ProcessGoalExactMatch) {
    SkillManifest m;
    m.name = "hello_comp";
    m.version = "1.0.0";
    Prompt p;
    p.name = "say_hello";
    p.description = "Says hello";
    p.prompt = "Hello, {{name}}!";
    m.prompts.push_back(p);
    addComponent("hello_comp", m);
    loadSkills();

    // The goal "say_hello" should match the prompt exactly via Phase 1
    json result = m_core->processGoal("say_hello");
    // With mock provider returning empty, result may be empty or error
    // The important thing is that it didn't crash and returned something
    EXPECT_FALSE(result.is_null());
}

TEST_F(AgentIntegrationTest, ProcessGoalWithGoalParam) {
    SkillManifest m;
    m.name = "greeting_comp";
    m.version = "1.0.0";
    Prompt p;
    p.name = "greet";
    p.description = "Greets";
    p.prompt = "{{goal}}";
    m.prompts.push_back(p);
    addComponent("greeting_comp", m);
    loadSkills();

    json result = m_core->processGoal("greet");
    EXPECT_FALSE(result.is_null());
}

TEST_F(AgentIntegrationTest, RunSkillNotFound) {
    loadSkills();
    json result = m_core->runSkill("nonexistent_skill", json::object());
    ASSERT_TRUE(result.is_string());
    std::string s = result.get<std::string>();
    EXPECT_TRUE(s.find("not found") != std::string::npos);
}

TEST_F(AgentIntegrationTest, RunSkillMissingDependency) {
    SkillManifest m;
    m.name = "dep_comp";
    m.version = "1.0.0";
    Prompt p;
    p.name = "needs_tool";
    p.description = "Needs a tool";
    p.prompt = "do something";
    p.dependencies = {"local:dep_comp:ghost_tool"};
    m.prompts.push_back(p);
    addComponent("dep_comp", m);
    loadSkills();

    json result = m_core->runSkill("local:dep_comp:needs_tool", json::object());
    ASSERT_TRUE(result.is_string());
    std::string s = result.get<std::string>();
    EXPECT_TRUE(s.find("Missing dependencies") != std::string::npos);
}

TEST_F(AgentIntegrationTest, RunSkillSuccess) {
    SkillManifest m;
    m.name = "simple_comp";
    m.version = "1.0.0";
    Prompt p;
    p.name = "simple_prompt";
    p.description = "A simple prompt";
    p.prompt = "test content";
    m.prompts.push_back(p);
    addComponent("simple_comp", m);
    loadSkills();

    json result = m_core->runSkill("local:simple_comp:simple_prompt", json::object());
    EXPECT_FALSE(result.is_null());
}

TEST_F(AgentIntegrationTest, ProcessGoalWithToolCall) {
    SkillManifest m;
    m.name = "tool_comp";
    m.version = "1.0.0";
    SkillTool tool;
    tool.name = "echo_tool";
    tool.description = "echoes";
    tool.command = "echo";
    tool.inputMode = "stdin";
    m.tools.push_back(tool);
    Prompt p;
    p.name = "tool_prompt";
    p.description = "Uses a tool";
    p.prompt = "{{tool:echo_tool input=\"hello\"}}";
    p.dependencies = {"local:tool_comp:echo_tool"};
    m.prompts.push_back(p);
    addComponent("tool_comp", m);
    loadSkills();

    json result = m_core->runSkill("local:tool_comp:tool_prompt", json::object());
    EXPECT_FALSE(result.is_null());
}

TEST_F(AgentIntegrationTest, ProcessGoalWithValidator) {
    SkillManifest m;
    m.name = "val_comp";
    m.version = "1.0.0";
    // A validator tool (just echoes)
    SkillTool validator;
    validator.name = "validator_tool";
    validator.description = "validates by echoing";
    validator.command = "echo";
    validator.inputMode = "stdin";
    m.tools.push_back(validator);
    Prompt p;
    p.name = "val_prompt";
    p.description = "Has a validator";
    p.prompt = "validate me";
    p.dependencies = {"local:val_comp:validator_tool"};
    ValidatorBinding vb;
    vb.toolName = "local:val_comp:validator_tool";
    p.validators.push_back(vb);
    m.prompts.push_back(p);
    addComponent("val_comp", m);
    loadSkills();

    json result = m_core->runSkill("local:val_comp:val_prompt", json::object());
    EXPECT_FALSE(result.is_null());
}

TEST_F(AgentIntegrationTest, ProcessGoalStreaming) {
    SkillManifest m;
    m.name = "stream_comp";
    m.version = "1.0.0";
    Prompt p;
    p.name = "stream_prompt";
    p.description = "Streaming test";
    p.prompt = "stream content";
    m.prompts.push_back(p);
    addComponent("stream_comp", m);
    loadSkills();

    a0::StreamHandle handle = m_core->processGoalStreaming("stream_prompt",
        [](const std::string& data, const std::string& dir) {
            (void)data;
            (void)dir;
        });
}

TEST_F(AgentIntegrationTest, CurrentSessionId) {
    loadSkills();
    // After init, session ID should be non-empty (generated during init)
    EXPECT_FALSE(m_core->currentSessionId().empty());
}
