#include "core/driven_core.h"
#include "llm/llm_provider.h"
#include "mock/mock_persistence_store.h"
#include <gtest/gtest.h>
#include <vector>

using namespace a0;

// ---------------------------------------------------------------------------
// Mock LLM provider that returns pre-programmed events from tick()
// ---------------------------------------------------------------------------

class MockLlmProvider : public LlmProvider {
public:
    std::vector<mpsc::AppCoreEvent> m_responses;
    bool m_requestActive = false;
    int m_tickCalls = 0;
    std::vector<ToolSchema> m_lastToolSchemas;
    std::string m_lastSystemPrompt;

    void startRequest(const std::string& sys,
                       const std::vector<Message>&,
                       const std::vector<ToolSchema>& tools) override {
        m_lastSystemPrompt = sys;
        m_lastToolSchemas = tools;
        m_requestActive = true;
    }
    void startRequestStreaming(const std::string& sys,
                                const std::vector<Message>&,
                                const std::vector<ToolSchema>& tools) override {
        m_lastSystemPrompt = sys;
        m_lastToolSchemas = tools;
        m_requestActive = true;
    }
    std::vector<mpsc::AppCoreEvent> tick() override {
        if (!m_requestActive) return {};
        ++m_tickCalls;
        if (!m_responses.empty()) {
            auto ev = std::move(m_responses);
            return ev;
        }
        return {};
    }
    void cancel() override { m_requestActive = false; }
    bool active() const override { return m_requestActive; }
    int timeoutMs() const override { return -1; }
    void setMockUrl(const std::string&) override {}
};

// ============================================================================
// DrivenCore persistence tests
// ============================================================================

using Msg = a0::persistence::Message;

struct DrivenCorePersistenceTest : ::testing::Test {
    MockLlmProvider provider;
    a0::persistence::MockPersistenceStore store;
    int64_t sessionDbId;
    std::string sessionUuid = "persist-test-uuid";

    void SetUp() override {
        sessionDbId = store.createSession(sessionUuid, 0, 0, 1);
    }

    DrivenCore makeCore() {
        DrivenCore core(&provider, nullptr, &store);
        core.setSession(sessionDbId, sessionUuid);
        return core;
    }
};

TEST_F(DrivenCorePersistenceTest, UserMessagePersistedOnSubmit) {
    DrivenCore core = makeCore();
    core.submitGoal("list files");

    // submitGoal persists the user message immediately
    auto msgs = store.loadMessages(sessionDbId, std::nullopt);
    ASSERT_GE(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].role, "user");
    EXPECT_EQ(msgs[0].content, "list files");
}

TEST_F(DrivenCorePersistenceTest, AssistantResponsePersistedAfterRunSync) {
    provider.m_responses = {mpsc::Complete{0, "found 3 files"}};

    DrivenCore core = makeCore();
    std::string result = core.runSync("find log files");

    EXPECT_EQ(result, "found 3 files");

    auto msgs = store.loadMessages(sessionDbId, std::nullopt);
    ASSERT_GE(msgs.size(), 2u);
    EXPECT_EQ(msgs[0].role, "user");
    EXPECT_EQ(msgs[0].content, "find log files");
    EXPECT_EQ(msgs[1].role, "assistant");
}

TEST_F(DrivenCorePersistenceTest, NoPersistenceWithoutSession) {
    DrivenCore core(&provider, nullptr, &store);
    // Intentionally NOT calling setSession — m_sessionDbId stays 0
    core.submitGoal("hello");

    // With no session set, no messages should be persisted
    auto msgs = store.loadMessages(0, std::nullopt);
    EXPECT_TRUE(msgs.empty());
}

TEST_F(DrivenCorePersistenceTest, SessionSwitchPersistsToCorrectSession) {
    provider.m_responses = {mpsc::Complete{0, "done"}};
    int64_t session2Id = store.createSession("session-2", 0, 0, 1);

    // Goal 1 goes to session 1
    DrivenCore core(&provider, nullptr, &store);
    core.setSession(sessionDbId, sessionUuid);
    core.runSync("goal 1");

    // Switch to session 2
    provider.m_responses = {mpsc::Complete{0, "done2"}};
    core.setSession(session2Id, "session-2");
    core.runSync("goal 2");

    auto msgs1 = store.loadMessages(sessionDbId, std::nullopt);
    ASSERT_GE(msgs1.size(), 1u);
    EXPECT_EQ(msgs1[0].content, "goal 1");

    auto msgs2 = store.loadMessages(session2Id, std::nullopt);
    ASSERT_GE(msgs2.size(), 1u);
    EXPECT_EQ(msgs2[0].content, "goal 2");
}

TEST_F(DrivenCorePersistenceTest, ErrorEventFailsGoal) {
    provider.m_responses = {mpsc::Error{"test", 0, "something went wrong"}};

    DrivenCore core = makeCore();
    std::string result = core.runSync("do something");

    EXPECT_TRUE(result.find("ERROR:") != std::string::npos);
    EXPECT_TRUE(result.find("something went wrong") != std::string::npos);
}

TEST_F(DrivenCorePersistenceTest, CancelClearsState) {
    provider.m_responses = {};

    DrivenCore core = makeCore();
    core.submitGoal("test cancel");

    EXPECT_FALSE(core.idle());
    core.cancel();
    EXPECT_TRUE(core.idle());
    // Second cancel should be safe
    core.cancel();
    EXPECT_TRUE(core.idle());
}

TEST_F(DrivenCorePersistenceTest, SetSessionBeforeSubmit) {
    int64_t customSessionId = store.createSession("custom-uuid", 0, 0, 1);
    provider.m_responses = {mpsc::Complete{0, "custom result"}};

    DrivenCore core(&provider, nullptr, &store);
    core.setSession(customSessionId, "custom-uuid");
    std::string result = core.runSync("custom goal");

    EXPECT_EQ(result, "custom result");

    auto msgs = store.loadMessages(customSessionId, std::nullopt);
    ASSERT_GE(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].content, "custom goal");
}

TEST_F(DrivenCorePersistenceTest, SubmitFromIdleOnly) {
    DrivenCore core = makeCore();
    core.submitGoal("first");
    // Second submit should be ignored (not idle)
    core.submitGoal("second");
    auto msgs = store.loadMessages(sessionDbId, std::nullopt);
    EXPECT_EQ(msgs[0].content, "first");
}

TEST_F(DrivenCorePersistenceTest, NullSkillManagerDoesNotCrash) {
    provider.m_responses = {mpsc::Complete{0, "no skillmgr result"}};
    DrivenCore core(&provider, nullptr, &store);
    core.setSession(sessionDbId, sessionUuid);
    EXPECT_NO_FATAL_FAILURE(core.runSync("test"));
}

TEST_F(DrivenCorePersistenceTest, TickFromIdleReturnsEmpty) {
    DrivenCore core = makeCore();
    auto events = core.tick();
    EXPECT_TRUE(events.empty());
}

// ============================================================================
// Persona filtering tests (need a real SkillManager with manifests)
// ============================================================================

#include "skills/skills.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace a0::skills;

struct DrivenCorePersonaTest : ::testing::Test {
    MockLlmProvider provider;
    a0::persistence::MockPersistenceStore store;
    SkillManager* skillMgr = nullptr;
    std::string m_skillsRoot;
    std::string m_storeRoot;

    void SetUp() override {
        std::string pid = std::to_string(::getpid());
        m_skillsRoot = "/tmp/a0_persona_core_test_skills_" + pid;
        m_storeRoot = "/tmp/a0_persona_core_test_store_" + pid;
        fs::remove_all(m_skillsRoot);
        fs::remove_all(m_storeRoot);
        fs::create_directories(m_skillsRoot + "/system/task-manager");
        fs::create_directories(m_skillsRoot + "/system/fs");
        {
            nlohmann::json j;
            j["name"] = "task-manager";
            j["version"] = "1.0";
            j["ns"] = "system";
            j["tools"] = nlohmann::json::array();
            nlohmann::json addTask, listTasks, removeTask;
            addTask = {{"name", "add-task"}, {"description", "add a task"},
                       {"default", true}, {"systemTool", true},
                       {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}};
            listTasks = {{"name", "list-tasks"}, {"description", "list tasks"},
                         {"default", true}, {"systemTool", true},
                         {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}};
            removeTask = {{"name", "remove-task"}, {"description", "remove a task"},
                          {"default", true}, {"systemTool", true},
                          {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}};
            j["tools"].push_back(addTask);
            j["tools"].push_back(listTasks);
            j["tools"].push_back(removeTask);
            {
                std::ofstream f(m_skillsRoot + "/system/task-manager/skill.json");
                f << j.dump(2) << "\n";
            }
        }
        {
            nlohmann::json j;
            j["name"] = "fs";
            j["version"] = "1.0";
            j["ns"] = "system";
            j["tools"] = nlohmann::json::array();
            nlohmann::json readT, writeT, globT;
            readT = {{"name", "read"}, {"description", "read a file"},
                     {"default", true}, {"systemTool", true},
                     {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}};
            writeT = {{"name", "write"}, {"description", "write a file"},
                      {"default", true}, {"systemTool", true},
                      {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}};
            globT = {{"name", "glob"}, {"description", "glob for files"},
                     {"default", true}, {"systemTool", true},
                     {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}};
            j["tools"].push_back(readT);
            j["tools"].push_back(writeT);
            j["tools"].push_back(globT);
            {
                std::ofstream f(m_skillsRoot + "/system/fs/skill.json");
                f << j.dump(2) << "\n";
            }
        }
        skillMgr = new SkillManager(m_skillsRoot, m_storeRoot);
        skillMgr->loadAll();
    }

    void TearDown() override {
        delete skillMgr;
        fs::remove_all(m_skillsRoot);
        fs::remove_all(m_storeRoot);
    }

    DrivenCore makeCore() {
        DrivenCore core(&provider, skillMgr, &store);
        return core;
    }

    bool hasToolSchema(const std::vector<::ToolSchema>& schemas, const std::string& name) {
        for (const auto& ts : schemas) {
            if (ts.name == name) return true;
        }
        return false;
    }
};

TEST_F(DrivenCorePersonaTest, NoPersona_EmptySchemas) {
    auto core = makeCore();
    provider.m_responses = {mpsc::Complete{0, "done"}};
    core.runSync("test");
    EXPECT_TRUE(provider.m_lastToolSchemas.empty());
}

TEST_F(DrivenCorePersonaTest, FilterBySkills) {
    auto core = makeCore();
    core.setPersonaSkills({"system_task-manager"});
    provider.m_responses = {mpsc::Complete{0, "done"}};
    core.runSync("test");
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "add-task"));
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "list-tasks"));
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "remove-task"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "read"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "write"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "glob"));
}

TEST_F(DrivenCorePersonaTest, FilterByTools) {
    auto core = makeCore();
    core.setPersonaTools({"system_fs_read"});
    provider.m_responses = {mpsc::Complete{0, "done"}};
    core.runSync("test");
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "read"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "write"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "glob"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "add-task"));
}

TEST_F(DrivenCorePersonaTest, FilterBySkillsAndTools) {
    auto core = makeCore();
    core.setPersonaSkills({"system_task-manager"});
    core.setPersonaTools({"system_fs_glob"});
    provider.m_responses = {mpsc::Complete{0, "done"}};
    core.runSync("test");
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "add-task"));
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "list-tasks"));
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "remove-task"));
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "glob"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "read"));
    EXPECT_FALSE(hasToolSchema(provider.m_lastToolSchemas, "write"));
}

TEST_F(DrivenCorePersonaTest, FilterEmptyResults) {
    auto core = makeCore();
    core.setPersonaSkills({"system_nonexistent"});
    provider.m_responses = {mpsc::Complete{0, "done"}};
    core.runSync("test");
    EXPECT_TRUE(provider.m_lastToolSchemas.empty());
}

TEST_F(DrivenCorePersonaTest, SetPersonaSetters) {
    auto core = makeCore();
    core.setPersona("software-engineer");
    core.setPersonaSkills({"system_fs"});
    core.setPersonaTools({"system_task-manager_add-task"});
    provider.m_responses = {mpsc::Complete{0, "done"}};
    core.runSync("test");
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "read") ||
                hasToolSchema(provider.m_lastToolSchemas, "write") ||
                hasToolSchema(provider.m_lastToolSchemas, "glob"));
    EXPECT_TRUE(hasToolSchema(provider.m_lastToolSchemas, "add-task") ||
                hasToolSchema(provider.m_lastToolSchemas, "read"));
}
