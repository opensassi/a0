#include "skills/validation_engine.h"
#include "skills/skills.h"
#include "persistence/sqlite_store.h"
#include "shared/hex_session_id.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace a0::skills;
using namespace a0::persistence;

class ValidationEngineTest : public ::testing::Test {
protected:
    std::string m_dbPath;
    SqliteStore* m_store = nullptr;
    ValidationEngine* m_engine = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid()) + "_" + std::to_string(rand());
        std::string tmpDir = "/tmp/a0_val_test_" + pid;
        fs::create_directories(tmpDir);
        m_dbPath = tmpDir + "/test.db";
        m_store = new SqliteStore(m_dbPath);
        m_engine = new ValidationEngine(m_store);
    }

    void TearDown() override {
        delete m_engine;
        delete m_store;
        std::string dir = m_dbPath.substr(0, m_dbPath.rfind('/'));
        fs::remove_all(dir);
    }

    /// Insert a historical invocation into the store
    void addInvocation(const std::string& ns, const std::string& component,
                       const std::string& toolName,
                       const json& params,
                       const json& output) {
        int type = (ns == "system") ? 0 : (ns == "local" ? 1 : 2);
        int skillId = m_store->ensureSkill(type, component);
        BuildFingerprint fp;
        fp.binarySha1 = "test";
        int agentId = m_store->registerAgent(fp);
        std::string sessionUuid = generateHexSessionId();
        int64_t sessionId = m_store->createSession(sessionUuid, 0, 0, agentId);
        int64_t msgId = m_store->appendMessage(sessionId, std::nullopt, 0,
            "tool", "dummy", "", "", toolName, output.dump());
        m_store->appendInvocation(msgId, skillId, toolName,
                                  params.dump(), output.dump());
    }

    SkillManifest makeManifest(const std::string& name) {
        SkillManifest m;
        m.name = name;
        m.ns = SkillNamespace::LOCAL;
        m.version = "1.0.0";
        return m;
    }

    SkillManifest makeManifestWithTool(const std::string& name, const SkillTool& tool) {
        SkillManifest m = makeManifest(name);
        m.tools.push_back(tool);
        return m;
    }
};

TEST_F(ValidationEngineTest, NoLogsReturnsZero) {
    SkillManifest m = makeManifest("no_logs");
    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "no_logs", m, "abc123", report);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(report, "no historical logs — validation skipped");
}

TEST_F(ValidationEngineTest, ReplayMatchSucceeds) {
    SkillTool tool;
    tool.name = "echo_tool";
    tool.command = "echo hello";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("test_comp", tool);

    addInvocation("local", "test_comp", "echo_tool", json(), json("hello\n"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "test_comp", m, "abc123", report);
    EXPECT_EQ(rc, 0);
}

TEST_F(ValidationEngineTest, ReplayMismatchFails) {
    SkillTool tool;
    tool.name = "echo_tool2";
    tool.command = "echo expected_output";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("test_comp", tool);

    addInvocation("local", "test_comp", "echo_tool2", json(), json("wrong_output"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "test_comp", m, "abc123", report);
    EXPECT_EQ(rc, -1);
    EXPECT_NE(report.find("FAIL"), std::string::npos);
}

TEST_F(ValidationEngineTest, BridgeResolvesMismatch) {
    SkillTool tool;
    tool.name = "old_tool";
    tool.command = "echo '{\"result\":\"raw\"}'";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("bridge_comp", tool);

    CompatBridge bridge;
    bridge.toolName = "old_tool";
    bridge.bridgeCommand = "echo '{\"result\":\"transformed\"}'";
    bridge.since = "0.0.1";
    bridge.description = "adapts raw output format";
    m.compat.push_back(bridge);

    addInvocation("local", "bridge_comp", "old_tool", json(),
                  json::parse("{\"result\":\"transformed\"}"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "bridge_comp", m, "abc123", report);
    EXPECT_EQ(rc, 1);
}

TEST_F(ValidationEngineTest, UnknownToolInLogSkipped) {
    SkillManifest m = makeManifest("no_tools");
    addInvocation("local", "no_tools", "ghost_tool", json(), json("xyz"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "no_tools", m, "abc123", report);
    EXPECT_EQ(rc, 0);
}

TEST_F(ValidationEngineTest, JsonOutputHandled) {
    SkillTool tool;
    tool.name = "json_tool";
    tool.command = "echo '{\"key\":\"value\"}'";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("json_comp", tool);

    addInvocation("local", "json_comp", "json_tool", json(),
                  json::parse("{\"key\":\"value\"}"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "json_comp", m, "abc123", report);
    EXPECT_EQ(rc, 0);
}

TEST_F(ValidationEngineTest, SystemNamespaceValidation) {
    SkillManifest m = makeManifest("sys_comp");
    m.ns = SkillNamespace::SYSTEM;
    std::string report;
    int rc = m_engine->validate(SkillNamespace::SYSTEM, "sys_comp", m, "def456", report);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(report, "no historical logs — validation skipped");
}

TEST_F(ValidationEngineTest, GithubNamespaceValidation) {
    SkillManifest m = makeManifest("gh_comp");
    m.ns = SkillNamespace::GITHUB;
    std::string report;
    int rc = m_engine->validate(SkillNamespace::GITHUB, "gh_comp", m, "ghi789", report);
    EXPECT_EQ(rc, 0);
}

TEST_F(ValidationEngineTest, MultipleInvocationsAllMatch) {
    SkillTool tool;
    tool.name = "multi_tool";
    tool.command = "echo expected";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("multi_comp", tool);

    addInvocation("local", "multi_comp", "multi_tool", json(), json("expected\n"));
    addInvocation("local", "multi_comp", "multi_tool", json(), json("expected\n"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "multi_comp", m, "abc123", report);
    EXPECT_EQ(rc, 0);
}

TEST_F(ValidationEngineTest, MultipleInvocationsOneFails) {
    SkillTool tool;
    tool.name = "partial_tool";
    tool.command = "echo expected";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("partial_comp", tool);

    addInvocation("local", "partial_comp", "partial_tool", json(), json("expected\n"));
    addInvocation("local", "partial_comp", "partial_tool", json(), json("wrong"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "partial_comp", m, "abc123", report);
    EXPECT_EQ(rc, -1);
}

static bool g_bridgeCalled = false;
static std::string xMockBridge(const std::string& input) {
    g_bridgeCalled = true;
    return "transformed_result";
}

TEST_F(ValidationEngineTest, TransformCommandWorks) {
    SkillTool tool;
    tool.name = "transform_tool";
    tool.command = "echo raw_output";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("transform_comp", tool);

    CompatBridge bridge;
    bridge.toolName = "transform_tool";
    bridge.bridgeCommand = "echo '{\"result\":\"transformed_result\"}'";
    bridge.description = "test bridge";
    m.compat.push_back(bridge);

    addInvocation("local", "transform_comp", "transform_tool", json(),
                  json::parse("{\"result\":\"transformed_result\"}"));

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "transform_comp", m, "abc123", report);
    // Bridge should transform raw_output to transformed_result, making match succeed
    EXPECT_EQ(rc, 1);
}
