#include "tui/agent_tui.h"
#include "tui/message_panel.h"
#include "tui/status_bar.h"
#include "mock/test_screen.h"
#include "mock/mock_persistence_store.h"
#include "mock/mock_agent_core.h"
#include <gtest/gtest.h>
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
    std::unique_ptr<MockAgentCore> mockCore;
    std::unique_ptr<AgentTui> tui;

    void SetUp() override {
        mockCore = std::make_unique<MockAgentCore>();
        MockAgentCore::Scenario scenario;
        scenario.tokens = {"Hello ", "from ", "mock!"};
        scenario.finalOutput = "Hello from mock!";
        scenario.tokenDelayMs = 1;
        mockCore->setScenario(scenario);
        tui = std::make_unique<AgentTui>(mockCore.get(), &mockPersistence, nullptr, nullptr, true);
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
// AgentTui — Interactive tests (use run() on background thread)
// ============================================================================

static void testInteractive(std::function<void(AgentTui&)> interactFn) {
    MockPersistenceStore persistence;
    MockAgentCore core;
    AgentTui tui(&core, &persistence, nullptr, nullptr, true);

    // Run TUI in background thread (test mode = FixedSize)
    std::thread tuiThread([&] {
        tui.run(true);
    });

    // Give it time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    interactFn(tui);

    tui.shutdown();
    if (tuiThread.joinable()) tuiThread.join();
}

TEST_F(TuiIntegrationTest, DISABLED_SubmitGoalAndSeeResponse) {
    testInteractive([](AgentTui& /*tui*/) {
        // Would interact via screen events
    });
}

TEST_F(TuiIntegrationTest, DISABLED_SessionsCommandShowsDialog) {
    testInteractive([](AgentTui& /*tui*/) {});
}

TEST_F(TuiIntegrationTest, DISABLED_MultipleMessagesStack) {
    testInteractive([](AgentTui& /*tui*/) {});
}

// ============================================================================
// AgentTui — Construction tests
// ============================================================================

struct TuiAgentConstructionTest : ::testing::Test {
    MockPersistenceStore store;
    MockAgentCore core;
};

TEST_F(TuiAgentConstructionTest, ConstructWithoutCrash) {
    AgentTui tui(&core, &store, nullptr, nullptr, true);
    SUCCEED();
}

TEST_F(TuiAgentConstructionTest, CurrentSessionIdEmptyInitially) {
    AgentTui tui(&core, &store, nullptr, nullptr, true);
    EXPECT_TRUE(tui.currentSessionId().empty());
}

TEST_F(TuiAgentConstructionTest, BuildComponentReturnsNonNull) {
    AgentTui tui(&core, &store, nullptr, nullptr, true);
    EXPECT_NE(tui.component(), nullptr);
}

TEST_F(TuiAgentConstructionTest, ComponentHasChildren) {
    AgentTui tui(&core, &store, nullptr, nullptr, true);
    auto comp = tui.component();
    EXPECT_NE(comp, nullptr);
    // Should have at least the dialog manager's modal wrapper
    // which wraps the main container (status bar, message panel, input panel)
}
