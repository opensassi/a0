#include "driven_core.h"
#include "llm_provider.h"
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

    void startRequest(const std::string&, const std::vector<Message>&,
                       const std::vector<ToolSchema>&) override {
        m_requestActive = true;
    }
    void startRequestStreaming(const std::string&, const std::vector<Message>&,
                                const std::vector<ToolSchema>&) override {
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
    provider.m_responses = {mpsc::Complete{"found 3 files"}};

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
    provider.m_responses = {mpsc::Complete{"done"}};
    int64_t session2Id = store.createSession("session-2", 0, 0, 1);

    // Goal 1 goes to session 1
    DrivenCore core(&provider, nullptr, &store);
    core.setSession(sessionDbId, sessionUuid);
    core.runSync("goal 1");

    // Switch to session 2
    provider.m_responses = {mpsc::Complete{"done2"}};
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
    provider.m_responses = {mpsc::Error{"something went wrong"}};

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
    provider.m_responses = {mpsc::Complete{"custom result"}};

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
    provider.m_responses = {mpsc::Complete{"no skillmgr result"}};
    DrivenCore core(&provider, nullptr, &store);
    core.setSession(sessionDbId, sessionUuid);
    EXPECT_NO_FATAL_FAILURE(core.runSync("test"));
}

TEST_F(DrivenCorePersistenceTest, TickFromIdleReturnsEmpty) {
    DrivenCore core = makeCore();
    auto events = core.tick();
    EXPECT_TRUE(events.empty());
}
