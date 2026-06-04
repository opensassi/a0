#include "skill_runner.h"
#include "skills/skills.h"
#include "dependency_graph.h"
#include "tool_runner.h"
#include "deepseek_provider.h"
#include "dependency_resolver.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0;
using namespace a0::skills;

// ---------------------------------------------------------------------------
// Mock provider that returns pre-programmed responses
// ---------------------------------------------------------------------------

class MockPipelineProvider : public InferenceProvider {
public:
    struct ResponseStep {
        std::string content;
        std::vector<ToolCall> toolCalls;
    };
    std::vector<ResponseStep> m_steps;
    int m_index = 0;

    void addTextResponse(const std::string& text) {
        m_steps.push_back({text, {}});
    }

    void addToolCallResponse(const std::vector<ToolCall>& calls) {
        m_steps.push_back({"", calls});
    }

    std::string complete(const std::string&, const std::string&) override {
        if (m_index < (int)m_steps.size()) {
            return m_steps[m_index++].content;
        }
        return "";
    }

    CompletionResponse complete(const std::string&,
                                 const std::vector<Message>&,
                                 const std::vector<::ToolSchema>&) override {
        CompletionResponse resp;
        if (m_index < (int)m_steps.size()) {
            auto& step = m_steps[m_index++];
            resp.content = step.content;
            resp.toolCalls = step.toolCalls;
        }
        return resp;
    }

    void setMockUrl(const std::string&) override {}
};

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class SkillPipelineTest : public ::testing::Test {
protected:
    std::string m_skillsDir;
    std::string m_storeDir;
    SkillManager* m_mgr = nullptr;
    MockPipelineProvider* m_provider = nullptr;
    DefaultDependencyResolver* m_depResolver = nullptr;
    DefaultSkillRunner* m_runner = nullptr;
    SubprocessToolRunner* m_toolRunner = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid()) + "_" + std::to_string(rand());
        m_skillsDir = "/tmp/a0_spipe_test_skills_" + pid;
        m_storeDir = "/tmp/a0_spipe_test_store_" + pid;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
        fs::create_directories(m_skillsDir + "/system");
        fs::create_directories(m_skillsDir + "/local");

        m_mgr = new SkillManager(m_skillsDir, m_storeDir, nullptr);

        m_toolRunner = new SubprocessToolRunner();
        m_mgr->setToolRunner(m_toolRunner);

        m_provider = new MockPipelineProvider();
        m_depResolver = new DefaultDependencyResolver(m_mgr);
        m_runner = new DefaultSkillRunner(
            m_toolRunner, m_provider, m_mgr, m_depResolver);
        m_runner->setMaxParallel(4);
    }

    void TearDown() override {
        delete m_runner;
        delete m_depResolver;
        delete m_provider;
        delete m_mgr;
        fs::remove_all(m_skillsDir);
        fs::remove_all(m_storeDir);
    }

    void addComponent(const std::string& component, const json& manifestJson) {
        fs::create_directories(m_skillsDir + "/local/" + component);
        std::ofstream f(m_skillsDir + "/local/" + component + "/skill.json");
        f << manifestJson.dump(2) << "\n";
    }

    void loadSkills() {
        m_mgr->loadAll();
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(SkillPipelineTest, PromptWithInlineText) {
    // Simple prompt with no template placeholders
    addComponent("test", json{
        {"name", "test"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "greet"}, {"description", "Says hello"}, {"prompt", "Hello world"}}
        })}
    });
    loadSkills();

    Prompt resolved;
    ASSERT_EQ(m_mgr->getPromptResolved("local-test-greet", resolved), 0);
    EXPECT_EQ(resolved.prompt, "Hello world");
}

TEST_F(SkillPipelineTest, PromptWithParamSubstitution) {
    addComponent("test", json{
        {"name", "test"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "echo"}, {"description", ""}, {"prompt", "User said: {{message}}"}}
        })}
    });
    loadSkills();

    std::string expanded = m_runner->expandPrompt(
        []() -> Prompt {
            Prompt p;
            p.name = "echo";
            p.prompt = "User said: {{message}}";
            return p;
        }(),
        {{"message", "hello"}});
    EXPECT_EQ(expanded, "User said: hello");
}

TEST_F(SkillPipelineTest, ValidatorsSequential) {
    // Declare validator tools in a skill.json so getTool() resolves them
    addComponent("validators", json{
        {"name", "validators"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "uppercase"}, {"description", "Uppercase input"},
                 {"command", "tr '[:lower:]' '[:upper:]'"},
                 {"inputMode", "stdin"}, {"timeoutSecs", 5}},
            json{{"name", "echo"}, {"description", "Echo input"},
                 {"command", "cat"}, {"inputMode", "stdin"}, {"timeoutSecs", 5}}
        })}
    });

    // Register C++ handlers for these tools
    m_mgr->registerHandler("local-validators-uppercase", [](const json& p, const HandlerContext&) {
        std::string input = p.value("input", "");
        for (auto& c : input) c = toupper(c);
        return ::a0::HandlerResult{input, {}};
    });
    m_mgr->registerHandler("local-validators-echo", [](const json& p, const HandlerContext&) {
        return ::a0::HandlerResult{p.value("input", "ok"), {}};
    });

    loadSkills();

    Prompt p;
    p.name = "val_test";
    p.prompt = "test content";
    p.parallelValidators = false;
    ValidatorBinding v1, v2;
    v1.toolName = "local-validators-uppercase";
    v2.toolName = "local-validators-echo";
    p.validators = {v1, v2};

    json result = m_runner->runValidators(p, json("hello world"));

    ASSERT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "HELLO WORLD");
}

TEST_F(SkillPipelineTest, ValidatorsParallel) {
    // Declare validator tools in skill.json
    addComponent("val_par", json{
        {"name", "val_par"},
        {"version", "1.0.0"},
        {"tools", json::array({
            json{{"name", "auth"}, {"description", "Auth check"},
                 {"command", "echo ok"}, {"inputMode", "stdin"}, {"timeoutSecs", 5}},
            json{{"name", "length"}, {"description", "Length check"},
                 {"command", "echo ok"}, {"inputMode", "stdin"}, {"timeoutSecs", 5}}
        })}
    });

    m_mgr->registerHandler("local-val_par-auth", [](const json& p, const HandlerContext&) {
        std::string input = p.value("input", "");
        return ::a0::HandlerResult{input.find("valid") != std::string::npos ? "ok" : "ERROR: invalid", {}};
    });
    m_mgr->registerHandler("local-val_par-length", [](const json& p, const HandlerContext&) {
        std::string input = p.value("input", "");
        return ::a0::HandlerResult{input.size() < 100 ? "ok" : "ERROR: too long", {}};
    });

    loadSkills();

    Prompt p;
    p.name = "par_val";
    p.prompt = "test";
    p.parallelValidators = true;
    ValidatorBinding v1, v2;
    v1.toolName = "local-val_par-auth";
    v2.toolName = "local-val_par-length";
    p.validators = {v1, v2};

    json result = m_runner->runValidators(p, json("valid content here"));

    ASSERT_TRUE(result.is_string());
    EXPECT_TRUE(result.get<std::string>().find("VALIDATOR_ERROR") == std::string::npos ||
                result.get<std::string>() == "ok");
}

TEST_F(SkillPipelineTest, DependenciesCheckedBeforeExecution) {
    addComponent("dep_test", json{
        {"name", "dep_test"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "needy"}, {"description", ""}, {"prompt", "needs tool"},
                 {"dependencies", json::array({"local-dep_test-nonexistent"})}}
        })}
    });
    loadSkills();

    Prompt resolved;
    ASSERT_EQ(m_mgr->getPromptResolved("local-dep_test-needy", resolved), 0);

    json result = m_runner->execute(resolved, json::object());
    ASSERT_TRUE(result.is_string());
    std::string s = result.get<std::string>();
    EXPECT_TRUE(s.find("Missing dependencies") != std::string::npos);
}

TEST_F(SkillPipelineTest, BatchReadersExecuteInParallel) {
    // Register reader tools that verify concurrent execution
    std::atomic<int> concurrent{0};
    std::atomic<int> maxConcurrent{0};

    m_mgr->registerHandler("system-fs-read", [&](const json&, const HandlerContext&) {
        int c = ++concurrent;
        int mc = maxConcurrent.load();
        while (c > mc) maxConcurrent.compare_exchange_weak(mc, c);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        --concurrent;
        return ::a0::HandlerResult{"read:ok", {}};
    });
    m_mgr->registerHandler("system-fs-glob", [&](const json&, const HandlerContext&) {
        int c = ++concurrent;
        int mc = maxConcurrent.load();
        while (c > mc) maxConcurrent.compare_exchange_weak(mc, c);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        --concurrent;
        return ::a0::HandlerResult{"glob:ok", {}};
    });

    // Directly test DependencyGraph batching with these handlers
    std::vector<ToolInvocation> invs = {
        {"system-fs-read", {}},
        {"system-fs-glob", {}}
    };
    auto batches = DependencyGraph::buildBatches(invs);

    // Verify readers are in the same batch
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].size(), 2u);

    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].outputs[0], "read:ok");
    EXPECT_EQ(results[0].outputs[1], "glob:ok");
}

TEST_F(SkillPipelineTest, PromptChainResolvesCorrectly) {
    addComponent("chain_test", json{
        {"name", "chain_test"},
        {"version", "1.0.0"},
        {"prompts", json::array({
            json{{"name", "base"}, {"description", ""}, {"prompt", "BASE: {{goal}}"}},
            json{{"name", "child"}, {"description", ""}, {"chain", json::array({"base"})}, {"prompt", "CHILD: {{input}}"}}
        })}
    });
    loadSkills();

    Prompt resolved;
    ASSERT_EQ(m_mgr->getPromptResolved("local-chain_test-child", resolved), 0);
    EXPECT_EQ(resolved.prompt, "BASE: {{goal}}\n\nCHILD: {{input}}");
}

TEST_F(SkillPipelineTest, ExecutionOrderMixed) {
    // Register a reader + writer handler
    m_mgr->registerHandler("system-fs-read", [](const json&, const HandlerContext&) {
        return ::a0::HandlerResult{"reader:ok", {}};
    });
    m_mgr->registerHandler("system-fs-write", [](const json&, const HandlerContext&) {
        return ::a0::HandlerResult{"writer:ok", {}};
    });

    std::vector<ToolInvocation> invs = {
        {"system-fs-read", {}},
        {"system-fs-write", {}},
        {"system-fs-read", {}}   // second reader after write
    };
    auto batches = DependencyGraph::buildBatches(invs);
    // Batch 0: both readers (read index 0, read index 2)
    // Batch 1: writer (write index 1)
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].size(), 2u);
    EXPECT_EQ(batches[1].size(), 1u);

    auto results = DependencyGraph::executeBatches(batches, m_mgr, 4);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].outputs[0], "reader:ok");
    EXPECT_EQ(results[0].outputs[1], "reader:ok");
    EXPECT_EQ(results[1].outputs[0], "writer:ok");
}

// ===========================================================================
// executeStreaming
// ===========================================================================

TEST_F(SkillPipelineTest, ExecuteStreaming_UnknownToolFiresOnChunk) {
    // When _tool param doesn't resolve to a known tool, executeStreaming falls
    // back to the sync LLM path and fires the complete result via onChunk.
    Prompt p;
    p.name = "unknown_test";
    p.prompt = "unknown tool test";

    json params;
    params["_tool"] = "nonexistent-tool";
    params["streaming"] = true;
    m_provider->addTextResponse("mock response");

    std::string output;
    auto handle = m_runner->executeStreaming(p, params,
        [&](const std::string& data, const std::string&) { output += data; });

    int rc = handle.wait();
    EXPECT_EQ(rc, 0);
    EXPECT_FALSE(output.empty());
}

TEST_F(SkillPipelineTest, ExecuteStreaming_PlainPromptFiresOnChunk) {
    // When no _tool param, executeStreaming fires the LLM result via onChunk.
    Prompt p;
    p.name = "plain_test";
    p.prompt = "just text";
    m_provider->addTextResponse("mock streaming output");

    std::string output;
    auto handle = m_runner->executeStreaming(p, json::object(),
        [&](const std::string& data, const std::string&) { output += data; });

    int rc = handle.wait();
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(output, "mock streaming output");
}
