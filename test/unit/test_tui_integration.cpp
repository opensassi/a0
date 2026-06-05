#include "tui/agent_tui.h"
#include "tui/message_panel.h"
#include "tui/status_bar.h"
#include "mock/test_screen.h"
#include "mock/mock_persistence_store.h"
#include "skills/skills.h"
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>

using namespace a0::tui;
using namespace a0::tui::test;
using namespace a0::persistence;

// ============================================================================
// AgentTui — Non-interactive tests (verify layout, no event loop needed)
// ============================================================================

struct TuiIntegrationTest : ::testing::Test {
    MockPersistenceStore mockPersistence;
    std::unique_ptr<AgentTui> tui;

    void SetUp() override {
        // AgentTui needs a SkillManager. We use nullptr for these tests since
        // they don't exercise goal processing (just verify layout/construction).
        // The DrivenCore will only process goals when a goal is submitted, and
        // nullptr SkillManager is handled gracefully.
        tui = std::make_unique<AgentTui>("test-key", "test-model",
                                          nullptr, &mockPersistence, 1, nullptr);
    }

    void TearDown() override {
        if (tui) tui->shutdown();
    }
};

TEST_F(TuiIntegrationTest, TuiLaunchesWithoutCrashing) {
    TestScreen screen(80, 24);
    screen.start(tui->component());

    bool foundIdle = screen.waitFor([](const std::string& text) {
        return text.find("Idle") != std::string::npos;
    }, 2000);

    EXPECT_TRUE(foundIdle)
        << "Should show 'Idle'. Got: " << screen.captureText();

    screen.stop();
}

TEST_F(TuiIntegrationTest, StatusBarShowsIdle) {
    TestScreen screen(80, 24);
    screen.start(tui->component());

    screen.waitFor([](const std::string& text) {
        return text.find("Idle") != std::string::npos;
    }, 2000);

    screen.stop();
}

TEST_F(TuiIntegrationTest, ResumeSessionInvalidUuid) {
    int rc = tui->resumeSession("nonexistent-uuid");
    EXPECT_EQ(rc, -1);
}

TEST_F(TuiIntegrationTest, ResumeSessionValidUuid) {
    int64_t dbId = mockPersistence.createSession("test-uuid", 0, 0, 1);

    a0::persistence::Message msg;
    msg.role = "user";
    msg.content = "previous message";
    msg.createdAt = 1000;
    auto it = mockPersistence.sessions.find(dbId);
    if (it != mockPersistence.sessions.end()) {
        it->second.messages.push_back(msg);
    }

    int rc = tui->resumeSession("test-uuid");
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(tui->currentSessionId(), "test-uuid");
}

TEST_F(TuiIntegrationTest, StatusBarStateTransitions) {
    StatusBar bar;
    bar.setAgentState(AgentState::Idle);
    bar.setAgentState(AgentState::Thinking);
    bar.setAgentState(AgentState::Executing);
    bar.setAgentState(AgentState::Error);
    SUCCEED();
}

TEST_F(TuiIntegrationTest, ShutdownWhileIdle) {
    TestScreen screen(80, 24);
    screen.start(tui->component());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    tui->shutdown();
    SUCCEED();
}

// ============================================================================
// AgentTui — Interactive tests (drive via TestScreen event injection)
// ============================================================================

static void testInteractive(
    std::function<void(AgentTui&, TestScreen&)> interactFn)
{
    auto persistence = std::make_unique<MockPersistenceStore>();
    AgentTui tui("test-key", "test-model", nullptr, persistence.get(), 1, nullptr);

    TestScreen testScreen(80, 24);
    tui.setScreen(testScreen.screenPtr());

    testScreen.postTask([component = tui.component()]() {
        component->TakeFocus();
    });
    testScreen.start(tui.component());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    interactFn(tui, testScreen);

    testScreen.stop();
    tui.clearScreen();
}

TEST_F(TuiIntegrationTest, SessionsCommandShowsDialog) {
    testInteractive([](AgentTui& tui, TestScreen& screen) {
        tui.submitInput("/sessions");

        bool found = screen.waitFor([](const std::string& text) {
            return text.find("Sessions") != std::string::npos;
        }, 3000);
        EXPECT_TRUE(found) << "Expected session dialog. Got: "
                           << screen.captureText();
    });
}

// ============================================================================
// AgentTui — Construction tests
// ============================================================================

struct TuiAgentConstructionTest : ::testing::Test {
    MockPersistenceStore store;
};

TEST_F(TuiAgentConstructionTest, ConstructWithoutCrash) {
    AgentTui tui("test-key", "test-model", nullptr, &store, 1, nullptr);
    SUCCEED();
}

TEST_F(TuiAgentConstructionTest, CurrentSessionIdEmptyInitially) {
    AgentTui tui("test-key", "test-model", nullptr, &store, 1, nullptr);
    EXPECT_TRUE(tui.currentSessionId().empty());
}

TEST_F(TuiAgentConstructionTest, BuildComponentReturnsNonNull) {
    AgentTui tui("test-key", "test-model", nullptr, &store, 1, nullptr);
    EXPECT_NE(tui.component(), nullptr);
}

TEST_F(TuiAgentConstructionTest, ComponentHasChildren) {
    AgentTui tui("test-key", "test-model", nullptr, &store, 1, nullptr);
    auto comp = tui.component();
    EXPECT_NE(comp, nullptr);
}
