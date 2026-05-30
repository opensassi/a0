#include "skills/validation_engine.h"
#include "skills/skills.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace a0::skills;

class ValidationEngineTest : public ::testing::Test {
protected:
    std::string m_logDir;
    std::string m_skillsDir;
    ValidationEngine* m_engine = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid());
        m_logDir = "/tmp/a0_val_test_logs_" + pid;
        m_skillsDir = "/tmp/a0_val_test_skills_" + pid;
        fs::remove_all(m_logDir);
        fs::remove_all(m_skillsDir);
        fs::create_directories(m_logDir);
        fs::create_directories(m_skillsDir);
        m_engine = new ValidationEngine(m_logDir);
    }

    void TearDown() override {
        delete m_engine;
        fs::remove_all(m_logDir);
        fs::remove_all(m_skillsDir);
    }

    void writeLog(const std::string& ns, const std::string& component,
                  const std::string& sessionId, const std::string& line) {
        fs::create_directories(m_logDir + "/" + ns + ":" + component);
        std::ofstream f(m_logDir + "/" + ns + ":" + component + "/" + sessionId + ".jsonl", std::ios::app);
        f << line << "\n";
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

    writeLog("local", "test_comp", "session1",
             R"({"tool":"echo_tool","params":"","output":"hello\n","ts":1000})");

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

    writeLog("local", "test_comp", "session1",
             R"({"tool":"echo_tool2","params":"","output":"wrong_output","ts":1000})");

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "test_comp", m, "abc123", report);
    EXPECT_EQ(rc, -1);
    EXPECT_NE(report.find("FAIL"), std::string::npos);
}

TEST_F(ValidationEngineTest, BridgeResolvesMismatch) {
    // New tool produces "raw" but old logs expect "transformed"
    SkillTool tool;
    tool.name = "old_tool";
    tool.command = "echo '{\"result\":\"raw\"}'";
    tool.inputMode = "stdin";
    SkillManifest m = makeManifestWithTool("bridge_comp", tool);

    // Bridge transforms input (old params) to produce expected output
    // Bridge command: echo '{"result":"transformed"}' (replaces raw with transformed)
    CompatBridge bridge;
    bridge.toolName = "old_tool";
    bridge.bridgeCommand = "echo '{\"result\":\"transformed\"}'";
    bridge.since = "0.0.1";
    bridge.description = "adapts raw output format";
    m.compat.push_back(bridge);

    writeLog("local", "bridge_comp", "session1",
             R"({"tool":"old_tool","params":"","output":{"result":"transformed"},"ts":1000})");

    std::string report;
    int rc = m_engine->validate(SkillNamespace::LOCAL, "bridge_comp", m, "abc123", report);
    EXPECT_EQ(rc, 1);  // bridges were used
}

TEST_F(ValidationEngineTest, UnknownToolInLogSkipped) {
    SkillManifest m = makeManifest("no_tools");
    writeLog("local", "no_tools", "session1",
             R"({"tool":"ghost_tool","params":"","output":"xyz","ts":1000})");

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

    writeLog("local", "json_comp", "session1",
             R"({"tool":"json_tool","params":"","output":{"key":"value"},"ts":1000})");

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
