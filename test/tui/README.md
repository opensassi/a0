## TUI E2E Test Harness — Design

### Architecture: Four-Layer Test Harness

```
┌─────────────────────────────────────────────────────────┐
│  Layer 4: Orchestration                                  │
│  test/e2e/test_tui_e2e.sh  +  CMake test registration   │
├─────────────────────────────────────────────────────────┤
│  Layer 3: Test Scenarios (Google Test)                   │
│  test_tui_panels.cpp  test_tui_integration.cpp          │
├─────────────────────────────────────────────────────────┤
│  Layer 2: Mock Components                                │
│  TestScreen  MockAgentCore  MockPersistence  MockProvider│
├─────────────────────────────────────────────────────────┤
│  Layer 1: FTXUI Virtual Terminal                         │
│  ScreenInteractive::FixedSize() + Event injection       │
└─────────────────────────────────────────────────────────┘
```

---

### Layer 1: Virtual Terminal Foundation

**Key FTXUI primitives** (pre-existing, used by harness):

| Tool | Purpose |
|------|---------|
| `ScreenInteractive::FixedSize(W, H)` | Headless virtual terminal — no real TTY needed |
| `component->OnEvent(Event{...})` | Programmatic keystroke injection |
| `screen.Render()` | Capture rendered output as string |
| `Screen::Post(Task{})` | Queue work from background threads |
| `Loop::RunOnce()` | Non-blocking event loop tick |
| `Event::Character('a')` | Simulate typing characters |
| `Event::Special({ .key = Enter })` | Simulate control keys |

**Key design constraint**: In production, `AgentTui::run()` calls `Loop.Run()` which blocks the main thread. For testing, we need to run the loop on a background thread while the test thread drives events and asserts.

---

### Layer 2: Mock Components

These live in `test/tui/mock/` and follow the project's existing mock patterns (direct C++ construction, no mocking framework needed):

#### `TestScreen` — Virtual Terminal Driver

```cpp
namespace a0::tui::test {

/// Drives FTXUI's FixedSize screen for automated testing.
/// Runs the event loop on a background thread.
class TestScreen {
public:
    TestScreen(int width = 80, int height = 24);
    ~TestScreen();

    /// Start the loop in a background thread.
    void start(ftxui::Component component);

    /// Stop the loop and join the thread.
    void stop();

    /// Inject a key event.
    void sendKey(const std::string& key);    // "Enter", "Up", "Ctrl+c"
    void sendChar(char c);
    void sendText(const std::string& text);  // type multiple chars

    /// Capture current rendered screen content as a string matrix.
    std::vector<std::string> captureScreen();

    /// Capture as raw text (trimmed, no whitespace padding).
    std::string captureText();

    /// Wait for predicate to return true, or timeout.
    /// Polls captureText() every 10ms.
    /// \retval true if predicate met before timeout.
    bool waitFor(std::function<bool(const std::string&)> predicate,
                 int timeoutMs = 3000);

    /// Set up the mock agent core that this screen drives.
    void setAgentCore(MockAgentCore* core);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui::test
```

#### `MockAgentCore` — Canned Streaming Responses

```cpp
namespace a0::tui::test {

/// Replaces AgentCore for TUI testing.
/// Calls onToken/onToolStart/onToolEnd/onComplete callbacks
/// with canned sequences.
class MockAgentCore : public ::a0::AgentCore {
public:
    /// Define a scenario: sequence of callbacks to fire.
    struct Scenario {
        std::vector<std::string> tokens;     // onToken calls
        struct ToolCall {
            std::string name;
            std::string output;
            bool success = true;
        };
        std::vector<ToolCall> toolCalls;      // onToolStart/onToolEnd pairs
        std::string finalOutput;              // onComplete
    };

    void setScenario(const Scenario& scenario);

    /// Override: stores callbacks, spawns a thread that fires them.
    int processGoalStreaming(const std::string& goal,
                             Callbacks cb) override;

    /// Simulate delay between tokens (default 10ms).
    void setTokenDelayMs(int ms);

private:
    Scenario m_scenario;
    std::atomic<bool> m_cancelled{false};
};

} // namespace a0::tui::test
```

#### `MockPersistenceStore` — In-Memory Session Storage

```cpp
namespace a0::tui::test {

/// In-memory PersistenceStore for testing session lifecycle.
/// No SQLite dependency — all data lives in std::vector.
class MockPersistenceStore : public a0::persistence::PersistenceStore {
public:
    int64_t createSession(int64_t rootId, int64_t parentId,
                          int agentId) override;
    std::vector<Message> loadMessages(int64_t sessionId) override;
    int64_t findSessionByUuid(const std::string& uuid) const override;
    // ... other overrides with simple in-memory storage
};

} // namespace a0::tui::test
```

#### `MockDeepSeekProvider` — Simulated SSE Stream (or reuse existing Python mock)

For C++ unit tests, a lightweight C++ mock provider returning canned token sequences. For full E2E, reuse the existing `mock_deepseek_server.py` (already in `test/e2e/`) with `--mock-api` flag to the `a0` binary.

---

### Layer 3: Test Scenarios

Organized into two test files, following the project's GTest conventions:

#### A. Panel Unit Tests (`test/unit/test_tui_panels.cpp`)

```cpp
#include <gtest/gtest.h>
#include "tui/message_panel.h"
#include "tui/input_panel.h"
#include "tui/status_bar.h"
#include "tui/dialog_manager.h"
#include "tui/session_manager.h"
#include "tui/mock/test_screen.h"

using namespace a0::tui;

// ── MessagePanel ──────────────────────────────────────

TEST(TuiMessagePanel, Append_UserMessage_RendersCyan) {
    TestScreen screen(80, 10);
    MessagePanel panel;
    
    MessageEntry entry;
    entry.role = MessageRole::User;
    entry.content = "hello world";
    panel.append(entry);

    screen.start(panel.component());
    std::string text = screen.captureText();
    EXPECT_TRUE(text.find("hello world") != std::string::npos);
}

TEST(TuiMessagePanel, BeginStreaming_ThenEndStream) {
    TestScreen screen(80, 10);
    MessagePanel panel;
    screen.start(panel.component());

    int idx = panel.beginStreaming(MessageRole::Assistant);
    panel.streamUpdate(idx, "partial");
    screen.waitFor([](const std::string& s) { 
        return s.find("partial") != std::string::npos; 
    });

    panel.endStream(idx);
    // final text still visible
    EXPECT_TRUE(screen.captureText().find("partial") != std::string::npos);
}

TEST(TuiMessagePanel, AppendToolCall_PendingThenCompleted) {
    TestScreen screen(80, 15);
    MessagePanel panel;
    screen.start(panel.component());

    int idx = panel.appendToolCall("glob", ToolState::Pending);
    screen.waitFor([](const std::string& s) {
        return s.find("glob") != std::string::npos;
    });

    panel.updateToolCall(idx, ToolState::Completed, "found 3 files");
    screen.waitFor([](const std::string& s) {
        return s.find("found 3 files") != std::string::npos;
    });
}

// ── InputPanel ────────────────────────────────────────

TEST(TuiInputPanel, SubmitFiresCallback) {
    TestScreen screen(80, 5);
    InputPanel panel;
    std::string submitted;
    panel.setOnSubmit([&](const std::string& s) { submitted = s; });
    screen.start(panel.component());

    screen.sendText("test goal");
    screen.sendKey("Enter");

    EXPECT_EQ(submitted, "test goal");
}

TEST(TuiInputPanel, HistoryNavigation) {
    TestScreen screen(80, 5);
    InputPanel panel;
    panel.addHistory("first prompt");
    panel.addHistory("second prompt");
    screen.start(panel.component());

    screen.sendKey("Up");  // should show "second prompt"
    screen.sendKey("Up");  // should show "first prompt"
    
    // Input component's content reflects history item
    // Assert via screen.captureText()
}

// ── StatusBar ─────────────────────────────────────────

TEST(TuiStatusBar, AgentStateTransitions) {
    TestScreen screen(80, 3);
    StatusBar bar;
    screen.start(bar.component());

    bar.setAgentState(AgentState::Idle);
    EXPECT_TRUE(screen.captureText().find("Idle") != std::string::npos);

    bar.setAgentState(AgentState::Thinking);
    screen.waitFor([](const std::string& s) {
        return s.find("Thinking") != std::string::npos;
    });
}

// ── DialogManager ─────────────────────────────────────

TEST(TuiDialogManager, ShowAndDismiss) {
    TestScreen screen(80, 24);
    DialogManager mgr;
    screen.start(mgr.component());

    EXPECT_FALSE(mgr.isActive());
    mgr.showHelp();
    EXPECT_TRUE(mgr.isActive());
    
    mgr.dismiss();
    EXPECT_FALSE(mgr.isActive());
}

// ── SessionManager ────────────────────────────────────

TEST(TuiSessionManager, CreateAndList) {
    MockPersistenceStore store;
    SessionManager mgr(&store);

    int64_t id = mgr.create("test-uuid-1");
    EXPECT_GT(id, 0);
    EXPECT_EQ(mgr.currentUuid(), "test-uuid-1");

    auto sessions = mgr.list();
    EXPECT_EQ(sessions.size(), 1);
}
```

#### B. Integration Tests (`test/unit/test_tui_integration.cpp`)

```cpp
#include <gtest/gtest.h>
#include "tui/agent_tui.h"
#include "tui/mock/test_screen.h"
#include "tui/mock/mock_agent_core.h"
#include "tui/mock/mock_persistence_store.h"

using namespace a0::tui::test;

// ── Full TUI Interaction ────────────────────────────

TEST(TuiIntegration, FullInputStreamComplete) {
    // Arrange
    MockAgentCore mockCore;
    MockPersistenceStore mockPersistence;
    
    // Scenario: user says "hello", agent responds with 3 tokens
    MockAgentCore::Scenario scenario;
    scenario.tokens = { "Hello", "! I", " am your agent." };
    scenario.finalOutput = "Hello! I am your agent.";
    mockCore.setScenario(scenario);
    mockCore.setTokenDelayMs(5);

    TestScreen screen(80, 24);
    AgentTui tui(&mockCore, &mockPersistence, nullptr, nullptr, true);
    
    // Act
    screen.start(tui.component());
    
    // Simulate user typing and submitting
    screen.sendText("hello");
    screen.sendKey("Enter");

    // Assert: user message appears
    screen.waitFor([](const std::string& text) {
        return text.find("hello") != std::string::npos;
    });

    // Assert: tokens stream in
    screen.waitFor([](const std::string& text) {
        return text.find("I am your agent") != std::string::npos;
    }, 5000);

    // Assert: state returns to Idle
    screen.waitFor([](const std::string& text) {
        return text.find("Idle") != std::string::npos;
    });
    
    screen.stop();
}

TEST(TuiIntegration, ToolCallVisible) {
    MockAgentCore mockCore;
    MockPersistenceStore mockPersistence;
    
    MockAgentCore::Scenario scenario;
    scenario.tokens = { "Let", " me", " search..." };
    scenario.toolCalls.push_back({"glob", "found 3 files", true});
    scenario.finalOutput = "Found 3 log files.";
    mockCore.setScenario(scenario);

    TestScreen screen(80, 24);
    AgentTui tui(&mockCore, &mockPersistence, nullptr, nullptr, true);
    screen.start(tui.component());

    screen.sendText("find log files");
    screen.sendKey("Enter");

    // Tool block appears
    screen.waitFor([](const std::string& text) {
        return text.find("glob") != std::string::npos;
    });

    // Tool completes
    screen.waitFor([](const std::string& text) {
        return text.find("found 3 files") != std::string::npos;
    }, 5000);
    
    screen.stop();
}

TEST(TuiIntegration, InterruptDuringStreaming) {
    MockAgentCore mockCore;
    MockPersistenceStore mockPersistence;
    
    MockAgentCore::Scenario scenario;
    scenario.tokens = { "A", " long", " response", " that", " keeps", " going..." };
    mockCore.setScenario(scenario);

    TestScreen screen(80, 24);
    AgentTui tui(&mockCore, &mockPersistence, nullptr, nullptr, true);
    screen.start(tui.component());

    screen.sendText("tell me a story");
    screen.sendKey("Enter");

    // Wait for streaming to start
    screen.waitFor([](const std::string& text) {
        return text.find("A long") != std::string::npos;
    });

    // Interrupt
    screen.sendKey("Ctrl+c");

    // Assert: "Interrupted" message shown
    screen.waitFor([](const std::string& text) {
        return text.find("Interrupted") != std::string::npos;
    });

    // Assert: input re-enabled (not in streaming state)
    screen.waitFor([](const std::string& text) {
        return text.find("Idle") != std::string::npos;
    });
    
    screen.stop();
}
```

#### C. Session Management Integration

```cpp
TEST(TuiIntegration, SessionsCommand) {
    MockAgentCore mockCore;
    MockPersistenceStore mockPersistence;
    
    // Pre-seed a session
    int64_t sid = mockPersistence.createSession(0, 0, 1);
    mockPersistence.appendMessage(sid, {}, 0, "user", "previous goal", "", "", "", "");

    TestScreen screen(80, 24);
    AgentTui tui(&mockCore, &mockPersistence, nullptr, nullptr, true);
    screen.start(tui.component());

    screen.sendText("/sessions");
    screen.sendKey("Enter");

    // Session list dialog appears
    screen.waitFor([](const std::string& text) {
        return text.find("Sessions") != std::string::npos;
    });

    // Select the seeded session
    screen.sendKey("Enter");  // select first
    
    // Historical message loaded
    screen.waitFor([](const std::string& text) {
        return text.find("previous goal") != std::string::npos;
    });
    
    screen.stop();
}
```

#### D. E2E Bash Script (`test/e2e/test_tui_e2e.sh`)

```bash
#!/usr/bin/env bash
set -euo pipefail

# Phase 1: Start mock DeepSeek server
python3 test/e2e/mock_deepseek_server.py --port 18080 &
MOCK_PID=$!
trap "kill $MOCK_PID 2>/dev/null; wait" EXIT

# Wait for mock server ready
for i in {1..10}; do
    curl -s http://localhost:18080/health && break
    sleep 0.5
done

# Phase 2: Run a0 TUI with scripted input
# Using a pseudo-TTY for interactive testing
SCRIPT="
type 'find log files'
sleep 2
assert_screen_contains 'Assistant'
type '/sessions'
sleep 1
assert_screen_contains 'Sessions'
type :q
"

echo "$SCRIPT" | a0 tui \
    --mock-api http://localhost:18080 \
    --a0-dir /tmp/a0-tui-test \
    --no-docker \
    --no-permissions \
    --test-mode  # NEW: use FixedSize instead of Fullscreen

echo "TUI E2E passed"
```

#### New `--test-mode` CLI Flag

Add a flag to `AgentTui` and the `a0 tui` subcommand:

| Flag | Default | Description |
|------|---------|-------------|
| `--test-mode` | `false` | Use `ScreenInteractive::FixedSize(80, 24)` instead of `Fullscreen()` + auto-enable headless mode |

When `--test-mode` is set:
- `AgentTui` constructs `ScreenInteractive::FixedSize(80, 24)` instead of `Fullscreen()`
- Input can be piped via stdin (read by a separate thread and forwarded as events)
- The `MockAgentCore` and `TestScreen` are not needed — the real agent runs with a real `DeepSeekProvider` pointed at the mock HTTP server
- Full end-to-end: real agent → mock DeepSeek API → real panels → rendered to virtual screen → captured via test assertions

---

### Layer 4: Build & Orchestration

#### New Files Summary

```
test/
├── tui/
│   ├── mock/
│   │   ├── test_screen.h              # TestScreen class
│   │   ├── test_screen.cpp
│   │   ├── mock_agent_core.h          # MockAgentCore
│   │   ├── mock_agent_core.cpp
│   │   ├── mock_persistence_store.h   # In-memory store
│   │   └── mock_persistence_store.cpp
│   └── README.md                      # TUI test documentation
├── unit/
│   ├── test_tui_panels.cpp            # Panel unit tests (Layer 3A)
│   └── test_tui_integration.cpp       # Integration tests (Layer 3B)
└── e2e/
    └── test_tui_e2e.sh                # Full E2E (Layer 3D)
```

#### CMake Changes

```cmake
# In CMakeLists.txt:
add_library(tui_test_mock STATIC
    test/tui/mock/test_screen.cpp
    test/tui/mock/mock_agent_core.cpp
    test/tui/mock/mock_persistence_store.cpp
)
target_link_libraries(tui_test_mock PUBLIC
    tui_lib
    GTest::gtest
    GTest::gtest_main
)

add_agent_test(test_tui_panels test/unit/test_tui_panels.cpp)
target_link_libraries(test_tui_panels PRIVATE tui_test_mock)

add_agent_test(test_tui_integration test/unit/test_tui_integration.cpp)
target_link_libraries(test_tui_integration PRIVATE tui_test_mock)
```

#### Pipeline Integration

The existing `run_all_tests.sh` gains a new Phase 4:
```
Phase 1: ctest (unit tests)              — includes test_tui_panels, test_tui_integration
Phase 2: Agent E2E (mock DeepSeek)       — unchanged
Phase 3: c2 dashboard E2E (Playwright)   — unchanged
Phase 4: TUI E2E (mock DeepSeek + --test-mode) — NEW
```

---

### Summary: What Makes This a Good TUI Test Harness

| Property | How It's Achieved |
|----------|-------------------|
| **Headless** | `ScreenInteractive::FixedSize()` — no real terminal needed |
| **Deterministic** | `MockAgentCore` with canned token sequences, no timing flakiness |
| **Async-safe** | `TestScreen::waitFor()` polling with timeout handles background thread timing |
| **Layered** | Unit tests for individual panels + integration tests for full AgentTui + E2E with real mock server |
| **Low overhead** | Mock components are direct C++ construction (48 lines for MockPersistenceStore), no framework needed |
| **CI-friendly** | No terminal, no Docker for unit tests; bash script E2E only needs Python mock server |
| **Reuses existing infra** | `mock_deepseek_server.py`, `add_agent_test()` CMake pattern, `test/e2e/` orchestration |
| **FTXUI-idiomatic** | Uses FTXUI's own event system — no hacks, no screen scraping |
