# Technical Specification: Terminal UI (TUI) Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

This document specifies a **TUI (Terminal User Interface) sub-module** for the existing a0 C++17 agent. The sub-module provides an interactive full-screen terminal interface replacing the basic stdin/stdout REPL, modelled after opencode.ai's terminal UI.

**Purpose**: The TUI sub-module enables rich interactive agent sessions directly in the terminal. Users see a split-panel layout with a scrollable message history on top and a persistent input area at the bottom. Agent responses stream token-by-token, tool executions are visible as collapsible status cards, and message roles are color-coded (user, assistant, system, tool). It is linked in-process into the `a0` binary and activated via the `a0 tui` subcommand. The TUI is the sole controller when active — no remote operator competes for input focus.

**Key behaviors:**

- **Split-panel layout** — scrollable message panel (top, flex-grow) + fixed input bar (bottom)
- **Streaming responses** — LLM tokens arrive via in-process callback, posted to FTXUI's `Screen::Post(Task{})` for thread-safe re-render
- **Color-coded messages** — user (cyan), assistant (green), system (yellow), tool (blue), error (red)
- **Tool execution visibility** — collapsible blocks showing tool name, status spinner, stdout/stderr output, and diffs
- **Markdown rendering** — assistant messages rendered through MD4C into FTXUI elements (headings, bold/italic, code blocks, lists)
- **Input history** — Up/Down arrow navigation through previous prompts
- **Session management** — `/sessions` command lists and resumes past sessions from SQLite; new sessions created automatically on first input
- **Ctrl+C interrupt** — cancels in-flight LLM request or tool execution
- **Modal dialogs** — framework for permission prompts, confirmations, help overlay (future: permission prompts via SkillManager integration)
- **Status bar** — session UUID, agent state (idle/thinking/executing/error), b1 connection status, message count

**Dependencies on other sub-modules:**

- `AgentCore` / `SkillRunner` — for `processGoalStreaming()` with token/tool-lifecycle callbacks
- `DeepSeekProvider` — extended with `completeStreaming()` (SSE-based token streaming)
- `PersistenceStore` (SQLite) — for session list / resume via `loadMessages()`
- `CommandRunner` — for streaming tool output capture (via `runStreaming`)
- **FTXUI v6.1.9** — terminal UI framework (via FetchContent)
- **MD4C v0.5.2** — Markdown parser (via FetchContent or vendored)
- `b1` supervisor — optional; when b1 is running, status bar shows connection state

---

## 2. Component Specifications (C++ Interfaces)

All new classes are defined in the `a0::tui` namespace, declared in `src/tui/`.

### 2.1 Core Data Structures

```cpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include "nlohmann/json.hpp"

namespace a0::tui {

/// Message role for display styling.
enum class MessageRole {
    User,       // cyan
    Assistant,  // green
    Tool,       // blue
    System,     // yellow
    Error       // red
};

/// Lifecycle state of a tool call visible in the TUI.
enum class ToolState {
    Pending,
    Running,
    Completed,
    Failed
};

/// Agent processing state for the status bar.
enum class AgentState {
    Idle,
    Thinking,
    Executing,
    Error
};

/// A single message entry in the scrollback.
struct MessageEntry {
    MessageRole role;
    std::string content;            // plain text or markdown source
    std::string toolName;           // non-empty for Tool messages
    ToolState toolState             = ToolState::Completed;
    std::string toolOutput;         // captured stdout/stderr
    int64_t timestamp               = 0;
    bool collapsed                  = false;
    int64_t sessionId               = 0;
};

/// Summary info for session list display.
struct SessionInfo {
    std::string uuid;
    int64_t dbId;
    std::string startedAt;
    int messageCount;
};

} // namespace a0::tui
```

### 2.2 AgentTui (Facade)

```cpp
namespace a0::tui {

/// Main TUI orchestrator. Owns the FTXUI screen, loop, and all panels.
/// Constructed with references to agent components, then run() enters the event loop.
class AgentTui {
public:
    /// \param core       AgentCore for processGoalStreaming.
    /// \param persistence PersistenceStore for session list/resume.
    /// \param skills     SkillManager for tool schema display (future).
    /// \param b1Status   Function to query b1 connection status (optional).
    AgentTui(::a0::AgentCore* core,
             ::a0::persistence::PersistenceStore* persistence,
             ::a0::skills::SkillManager* skills,
             std::function<bool()> b1Status = nullptr);

    virtual ~AgentTui();

    /// Enter the FTXUI event loop. Blocks until user quits (Ctrl+Q / :q).
    /// \retval 0  Normal exit.
    /// \retval -1 FTXUI error.
    int run();

    /// Request graceful shutdown. Posts quit to the event loop.
    void shutdown();

    /// Resume an existing session by UUID.
    /// Loads messages from persistence into the message panel.
    /// \retval 0  Session loaded.
    /// \retval -1 Session not found.
    int resumeSession(const std::string& uuid);

    /// Get current session UUID.
    std::string currentSessionId() const;

private:
    ::a0::AgentCore* m_core;                    // non-owning
    ::a0::persistence::PersistenceStore* m_persistence; // non-owning
    ::a0::skills::SkillManager* m_skills;       // non-owning
    std::function<bool()> m_b1Status;

    std::unique_ptr<MessagePanel> m_messagePanel;
    std::unique_ptr<InputPanel> m_inputPanel;
    std::unique_ptr<StatusBar> m_statusBar;
    std::unique_ptr<DialogManager> m_dialogMgr;
    std::unique_ptr<SessionManager> m_sessionMgr;

    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    AgentState m_agentState = AgentState::Idle;
    bool m_streaming = false;

    // FTXUI components (lifetime managed by the library)
    ftxui::ScreenInteractive* m_screen = nullptr;
    ftxui::Component m_mainComponent;

    int xBuildLayout();
    int xHandleSubmit(const std::string& input);
    int xHandleInterrupt();
    int xHandleCommand(const std::string& cmd);
    int xProcessGoal(const std::string& goal);

    // Streaming callbacks — called from background threads
    void xOnToken(const std::string& token);
    void xOnToolStart(const std::string& name, const nlohmann::json& params);
    void xOnToolEnd(const std::string& name, const std::string& output, bool success);
    void xOnComplete(const std::string& fullOutput);
    void xOnError(const std::string& error);
};

} // namespace a0::tui
```

### 2.3 MessagePanel

```cpp
namespace a0::tui {

/// Scrollable message display panel.
/// Builds and maintains an FTXUI component tree of message elements.
/// All mutations must be posted to the FTXUI event loop via Screen::Post().
class MessagePanel {
public:
    MessagePanel();
    virtual ~MessagePanel();

    /// The FTXUI component (vertical container of message elements).
    ftxui::Component component() const;

    /// Append a complete message to the scrollback.
    int append(const MessageEntry& entry);

    /// Begin a streaming message — creates a placeholder that xStreamUpdate fills.
    /// \param role Typically Assistant.
    /// \retval index of the new entry for update calls.
    int beginStreaming(MessageRole role);

    /// Update the current streaming message with new tokens.
    /// \param index Returned by beginStreaming.
    int streamUpdate(int index, const std::string& text);

    /// Finalize a streaming message.
    int endStream(int index);

    /// Add a tool call display block.
    int appendToolCall(const std::string& name,
                       ToolState state,
                       const std::string& output = "");

    /// Update a tool call's state/output.
    int updateToolCall(int index, ToolState state, const std::string& output);

    /// Clear all messages.
    void clear();

    /// Scroll to bottom (called after new content).
    void scrollToBottom();

    /// Load historical messages for session resume.
    int loadHistory(const std::vector<::a0::persistence::Message>& messages);

    /// Number of visible messages.
    size_t count() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

### 2.4 InputPanel

```cpp
namespace a0::tui {

/// Fixed bottom input bar with prompt history.
class InputPanel {
public:
    InputPanel();
    virtual ~InputPanel();

    /// The FTXUI component (Input + optional submit hint).
    ftxui::Component component() const;

    /// Set callback when user submits input.
    void setOnSubmit(std::function<void(const std::string&)> cb);

    /// Set callback for interrupt (Ctrl+C during streaming).
    void setOnInterrupt(std::function<void()> cb);

    /// Enable/disable input (disabled during streaming).
    void setEnabled(bool enabled);

    /// Set placeholder text.
    void setPlaceholder(const std::string& text);

    /// Clear current input buffer.
    void clear();

    /// Focus the input element.
    void focus();

    /// Add a prompt to history.
    int addHistory(const std::string& prompt);

    /// Load history from JSONL file (future).
    int loadHistory(const std::string& path);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

### 2.5 StatusBar

```cpp
namespace a0::tui {

/// Fixed top bar showing session and agent state information.
class StatusBar {
public:
    StatusBar();
    virtual ~StatusBar();

    /// The FTXUI component.
    ftxui::Component component() const;

    /// Set the session UUID to display.
    void setSessionId(const std::string& uuid);

    /// Update agent state (idle/thinking/executing/error).
    void setAgentState(AgentState state);

    /// Set b1 connection status.
    void setB1Connected(bool connected);

    /// Set message count.
    void setMessageCount(size_t count);

    /// Show a transient status message (e.g., "Saved session").
    void showStatus(const std::string& msg, int timeoutSecs = 3);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

### 2.6 DialogManager

```cpp
namespace a0::tui {

/// Stack-based modal dialog system using FTXUI::Modal.
class DialogManager {
public:
    DialogManager();
    virtual ~DialogManager();

    /// The FTXUI component (main container with Modal overlay).
    ftxui::Component component() const;

    /// Show a dialog. Pushes onto the stack.
    /// \param dialog  FTXUI component to render as modal.
    /// \param onDismiss Callback when dialog is dismissed.
    int show(ftxui::Component dialog, std::function<void()> onDismiss = nullptr);

    /// Dismiss the topmost dialog.
    void dismiss();

    /// Dismiss all dialogs.
    void dismissAll();

    /// Whether any dialog is currently shown.
    bool isActive() const;

    /// Show the help overlay.
    int showHelp();

    /// Show a confirmation dialog.
    /// \param title, message   Display text.
    /// \param onConfirm        Called with true/false.
    int showConfirm(const std::string& title,
                    const std::string& message,
                    std::function<void(bool)> onConfirm);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

### 2.7 SessionManager

```cpp
namespace a0::tui {

/// Manages session lifecycle — create, list, resume via SQLite.
class SessionManager {
public:
    /// \param persistence PersistenceStore for session queries.
    SessionManager(::a0::persistence::PersistenceStore* persistence);
    virtual ~SessionManager();

    /// Create a new session. Returns DB id.
    int64_t create(const std::string& uuid);

    /// List all recent sessions.
    std::vector<SessionInfo> list(int limit = 20) const;

    /// Resume a session by UUID.
    /// \retval 0  Found.
    /// \retval -1 Not found.
    int resume(const std::string& uuid, int64_t& outDbId);

    /// Get current session UUID.
    std::string currentUuid() const;

    /// End the current session (sets ended_at).
    void endCurrent();

private:
    ::a0::persistence::PersistenceStore* m_persistence;
    std::string m_currentUuid;
    int64_t m_currentDbId = 0;
};

} // namespace a0::tui
```

### 2.8 MarkdownRenderer

```cpp
namespace a0::tui {

/// Converts Markdown text into an FTXUI Element tree.
/// Wraps MD4C parser; produces styled ftxui::Elements.
class MarkdownRenderer {
public:
    MarkdownRenderer();
    virtual ~MarkdownRenderer();

    /// Parse markdown and return an FTXUI element suitable for rendering.
    /// \param md         Markdown source text.
    /// \param streaming  If true, handles incomplete markdown gracefully.
    ftxui::Element render(const std::string& md, bool streaming = false);

    /// Render inline-only (for short snippets in tool blocks, etc.).
    ftxui::Element renderInline(const std::string& md);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

---

## 3. System Architecture (C4 Diagram)

```mermaid
graph TB
    subgraph "a0 Process (TUI Mode)"
        TUI[TUI Sub-module]
        AT[AgentTui]
        MP[MessagePanel]
        IP[InputPanel]
        SB[StatusBar]
        DM[DialogManager]
        SM[SessionManager]
        MR[MarkdownRenderer]

        AC[AgentCore]
        DP[DeepSeekProvider]
        SR[SkillRunner]
        PS[PersistenceStore]
        SK[SkillManager]
    end

    subgraph "External"
        LLM[DeepSeek API]
        B1[b1 Supervisor]
        C2[c2 Web Dashboard]
    end

    subgraph "Storage"
        DB[(.a0/db/sessions.db)]
    end

    User((User))

    User -->|keyboard input| TUI
    TUI --> AT
    AT --> MP
    AT --> IP
    AT --> SB
    AT --> DM
    AT --> SM
    AT --> MR

    AT -->|processGoalStreaming| AC
    AC --> DP
    AC --> SR
    AC --> SK
    AC --> PS
    PS --> DB

    DP -->|SSE streaming| LLM
    SK -->|tool execution| SR

    SB -.->|optional| B1
    B1 -.-> C2

    SM --> PS
    MP --> MR
```

**Caption**: The TUI sub-module runs in-process with AgentCore. User keyboard events flow from InputPanel → AgentTui → AgentCore::processGoalStreaming(). Token callbacks fire from DeepSeekProvider's SSE parser, posted via FTXUI's TaskQueue to MessagePanel for rendering. Tool lifecycle callbacks similarly update tool blocks. Session list/resume goes through PersistenceStore (SQLite). The b1/c2 pipeline is unaffected — the TUI is a local rendering layer, not an IPC peer.

---

## 4. Data Flow Diagrams

### 4.1 Full Interaction Cycle (User Input → Response)

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant AC as AgentCore
    participant DP as DeepSeekProvider
    participant SR as SkillRunner
    participant MP as MessagePanel
    participant SB as StatusBar

    User->>IP: type "find log files"
    User->>IP: press Enter
    IP->>AT: onSubmit("find log files")
    AT->>SB: setAgentState(Thinking)
    AT->>MP: append(User, "find log files")
    AT->>AC: processGoalStreaming with callbacks

    AC->>SR: execute("find_log_files", params)
    SR->>SR: expandPrompt, build messages
    SR->>DP: complete(messages, schemas)

    DP->>DP: SSE connection to DeepSeek API

    rect rgb(230, 255, 230)
        Note over DP,AT: Token streaming phase
        DP-->>AT: onToken("I'll")
        AT->>MP: streamUpdate("I'll")
        MP-->>AT: (FTXUI re-render via Screen::Post)
        DP-->>AT: onToken(" search")
        AT->>MP: streamUpdate("I'll search")
        DP-->>AT: onToken(" using")
        AT->>MP: streamUpdate("I'll search using")
        Note right of DP: ...continues for each token...
    end

    rect rgb(255, 240, 230)
        Note over SR,MP: Tool call phase
        SR->>AT: onToolStart("glob", {pattern:"*.log"})
        AT->>MP: appendToolCall("glob", Pending)
        AT->>SB: setAgentState(Executing)
        SR->>SR: execute tool "glob"
        SR-->>AT: onToolEnd("glob", output, success)
        AT->>MP: updateToolCall(index, Completed, output)
        AT->>SB: setAgentState(Thinking)
    end

    rect rgb(230, 255, 230)
        Note over DP,MP: Second response streaming
        DP-->>AT: onToken("Found")
        AT->>MP: streamUpdate("Found 3 log files")
        DP-->>AT: onComplete("Found 3 log files matching *.log")
    end

    AT->>MP: endStream(index)
    AT->>SB: setMessageCount(N)
    AT->>SB: setAgentState(Idle)
    AT->>IP: setEnabled(true)
    IP->>IP: clear()
    IP->>IP: focus()
```

### 4.2 Session List and Resume

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant SM as SessionManager
    participant PS as PersistenceStore
    participant DM as DialogManager
    participant MP as MessagePanel

    User->>IP: type "/sessions"
    IP->>AT: onSubmit("/sessions")
    AT->>AT: xHandleCommand("/sessions")
    AT->>SM: list(20)
    SM->>PS: loadSessions(20)
    PS-->>SM: [SessionInfo...]
    SM-->>AT: sessions

    AT->>DM: show session list dialog
    DM-->>User: (renders selectable list)

    User->>DM: select session "abc-123"
    DM->>AT: onSelect("abc-123")
    AT->>SM: resume("abc-123", outDbId)
    SM->>PS: findSessionByUuid("abc-123")
    PS-->>SM: 42
    SM-->>AT: 0 (success)

    AT->>MP: clear()
    AT->>PS: loadMessages(42)
    PS-->>AT: [Message...]
    AT->>MP: loadHistory(messages)
    AT->>SB: setSessionId("abc-123")
    AT->>SB: setMessageCount(N)
    DM->>AT: dismiss()
```

### 4.3 Interrupt Handling

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant DP as DeepSeekProvider
    participant SR as SkillRunner
    participant SB as StatusBar

    Note over AT: Streaming in progress

    User->>IP: Ctrl+C
    IP->>AT: onInterrupt()
    AT->>SB: setAgentState(Idle)
    AT->>DP: cancel()     // close SSE connection
    AT->>SR: cancelTools() // kill running tool subprocesses
    AT->>AT: append system message "Interrupted"
    AT->>SB: setAgentState(Idle)
    AT->>IP: setEnabled(true)
    AT->>IP: focus()
```

---

## 5. Configuration & CLI Extensions

### 5.1 New `tui` Subcommand

```
a0 tui [--resume <uuid>] [--no-permissions]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--resume <uuid>` | — | Resume an existing session on startup |
| `--no-permissions` | `false` | Auto-approve tool execution (current default behavior) |

### 5.2 In-TUI Commands

| Command | Description |
|---------|-------------|
| `/sessions` | List recent sessions, select to resume |
| `/help` | Show keybinding reference |
| `/clear` | Clear message panel (display only) |
| `/quit` or `Ctrl+Q` | Exit TUI |
| `/export` | Export current session as JSONL |
| `:q` | Same as `/quit` |

### 5.3 Keybindings

| Key | Action |
|-----|--------|
| `Enter` | Submit input |
| `Shift+Enter` | Newline in input |
| `Up/Down` | Navigate input history |
| `Ctrl+C` | Interrupt streaming / cancel |
| `Ctrl+Q` | Quit TUI |
| `Ctrl+L` | Redraw screen |
| `Tab` | (Future) Autocomplete |
| `Escape` | Close dialog / cancel |

### 5.4 Environment Variables

| Variable | Used by | Description |
|----------|---------|-------------|
| `A0_TUI_NO_PERMISSIONS` | AgentTui | Skip permission prompts (like `--no-permissions`) |

---

## 6. Testing Requirements

### 6.1 Unit Tests

| Class | Test Case | Verification |
|-------|-----------|-------------|
| `MessagePanel` | `append` User message | Renders cyan-colored text |
| `MessagePanel` | `append` Assistant message | Renders green-colored text |
| `MessagePanel` | `beginStreaming` + `streamUpdate` | Placeholder created, updated text rendered |
| `MessagePanel` | `endStream` | Streaming indicator removed, final text rendered |
| `MessagePanel` | `appendToolCall` with Pending -> Completed | Spinner stops, output displayed |
| `MessagePanel` | `loadHistory` with 10 messages | All 10 rendered in order |
| `MessagePanel` | `clear` | Count = 0 |
| `InputPanel` | `onSubmit` callback fires | Callback invoked with input text |
| `InputPanel` | `addHistory` + Up arrow | Previous input restored |
| `InputPanel` | `setEnabled(false)` | Input not accepting text |
| `StatusBar` | `setAgentState` all states | Correct label displayed for each |
| `StatusBar` | `setB1Connected` | Shows connected/disconnected indicator |
| `DialogManager` | `show` + `dismiss` | Dialog appears then disappears |
| `DialogManager` | `showConfirm` true path | onConfirm(true) called |
| `SessionManager` | `create` new session | Returns positive dbId |
| `SessionManager` | `list` with 5 sessions | Returns 5 items |
| `SessionManager` | `resume` existing UUID | 0, outDbId populated |
| `SessionManager` | `resume` missing UUID | -1 |
| `MarkdownRenderer` | `render` with heading | Bold large text element |
| `MarkdownRenderer` | `render` with code block | Dim background element |
| `MarkdownRenderer` | `render` with bold/italic | Correct decorators applied |
| `MarkdownRenderer` | `renderInline` | No block-level elements |
| `MarkdownRenderer` | `render` with incomplete markdown | Graceful handling (no crash) |

### 6.2 Integration Tests

| ID | Scenario | Steps | Expected |
|----|----------|-------|----------|
| INT‑TUI‑01 | TUI launches | Run `a0 tui` | FTXUI screen renders, status bar shows "Idle" |
| INT‑TUI‑02 | Submit a goal | Type "hello", press Enter | User message appears, agent responds |
| INT‑TUI‑03 | Streaming display | Send goal that triggers long response | Tokens appear incrementally in message panel |
| INT‑TUI‑04 | Tool execution visible | Send goal that triggers tool call | Tool block appears with name, status, output |
| INT‑TUI‑05 | Interrupt during streaming | Ctrl+C while agent is responding | Streaming stops, "Interrupted" message shown, input re-enabled |
| INT‑TUI‑06 | `/sessions` command | Type `/sessions` | Dialog shows session list |
| INT‑TUI‑07 | Resume session | Select a session from `/sessions` | Historical messages load, new input continues that session |
| INT‑TUI‑08 | Input history | Submit 3 prompts, press Up 3 times | All 3 prompts accessible in reverse order |
| INT‑TUI‑09 | `/help` command | Type `/help` | Help dialog with keybindings |
| INT‑TUI‑10 | `/quit` | Type `/quit` | TUI exits cleanly, terminal restored |
| INT‑TUI‑11 | Resume via flag | `a0 tui --resume <uuid>` | Previous session loaded, messages displayed |
| INT‑TUI‑12 | b1 status indicator | Start b1, launch TUI | Status bar shows "b1: ✓" |

---

## 7. Integration with Existing Main Specification

### 7.1 `main.cpp` Changes

1. **CLI11 subcommand**: Add `App tuiCmd = app.add_subcommand("tui", "Interactive terminal UI");` with `--resume` and `--no-permissions` flags.

2. **AgentStack reuse**: `AgentTui` is constructed using the same `AgentStack` pattern. Stack components (AgentCore, SkillManager, PersistenceStore, etc.) are wired identically to the existing `cmdRepl()` path, minus b1 launch (TUI will register with b1 if available, but doesn't depend on it):

```cpp
void cmdTui(const std::string& resumeUuid, bool noPermissions) {
    AgentStack stack = buildAgentStack();
    AgentTui tui(stack.core, stack.persistence, stack.skills,
                 [&]() { return b1Alive; });
    if (!resumeUuid.empty())
        tui.resumeSession(resumeUuid);
    tui.run();
}
```

3. **Streaming on AgentCore**: `AgentCore` gains a `processGoalStreaming()` overload accepting token/lifecycle callbacks. Internally routes through `SkillRunner::executeStreaming()` -> `DeepSeekProvider::completeStreaming()`.

### 7.2 `DeepSeekProvider` Changes

New method `completeStreaming()` that:
- Opens a libcurl connection to DeepSeek's chat/completions endpoint with `Accept: text/event-stream`
- Parses SSE `data:` lines incrementally (extracting `delta.content` chunks)
- Calls `m_onToken(std::string)` for each content delta
- Calls `m_onComplete(std::string)` when `[DONE]` signal received
- Supports `cancel()` via a shared atomic flag
- Timeout at 60s

### 7.3 `SkillRunner` Changes

`executeStreaming()` is no longer a stub. It:
- Accepts token callback and tool lifecycle callbacks
- Passes token callback through to `DeepSeekProvider::completeStreaming()`
- Calls `onToolStart(name, params)` before tool execution
- Calls `onToolEnd(name, output, success)` after tool execution
- Falls back to non-streaming `execute()` when tool-calling loop requires sequential LLM rounds

### 7.4 Build System

`src/tui/CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v6.1.9
)
FetchContent_Declare(md4c
  GIT_REPOSITORY https://github.com/mity/md4c
  GIT_TAG release-0.5.2
)
FetchContent_MakeAvailable(ftxui md4c)

add_library(tui_lib STATIC
    agent_tui.cpp
    message_panel.cpp
    input_panel.cpp
    status_bar.cpp
    dialog_manager.cpp
    session_manager.cpp
    markdown_renderer.cpp
    styles.cpp
)
target_include_directories(tui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tui_lib PUBLIC
    ftxui::ftxui
    md4c::md4c
    a0_lib
    persistence_lib
    nlohmann_json::nlohmann_json
)
```

Root `CMakeLists.txt` adds `add_subdirectory(src/tui)` and links `tui_lib` to the `a0` executable.

### 7.5 Project File Layout

```
src/tui/
├── CMakeLists.txt              # FTXUI + MD4C deps, tui_lib build
├── technical-specification.md  # this document
├── styles.h                    # Color/decorator constants
├── styles.cpp
├── agent_tui.h                 # Facade
├── agent_tui.cpp
├── message_panel.h             # Scrollable message display
├── message_panel.cpp
├── input_panel.h               # Input bar with history
├── input_panel.cpp
├── status_bar.h                # Session/agent status bar
├── status_bar.cpp
├── dialog_manager.h            # Modal dialog system
├── dialog_manager.cpp
├── session_manager.h           # Session lifecycle
├── session_manager.cpp
├── markdown_renderer.h         # MD4C -> FTXUI element
├── markdown_renderer.cpp
```

---

## 8. Implementation Outline

### Phase 1: Dependency Integration

- Add FTXUI v6.1.9 and MD4C v0.5.2 via `FetchContent` in root `CMakeLists.txt`
- Create `src/tui/CMakeLists.txt` with `tui_lib` static library
- Add `add_subdirectory(src/tui)` to root CMake
- Verify: `cmake -B build && cmake --build build` produces `tui_lib` (empty sources initially)

### Phase 2: Core TUI Scaffold

- Implement `styles.h/.cpp` -- color constants, role-to-decorator map
- Implement `MessagePanel` -- component tree builder, append/stream/clear
- Implement `InputPanel` -- FTXUI Input wrapper, history, submit callback
- Implement `StatusBar` -- info bar with agent state, session ID
- Implement `AgentTui` shell -- wires panels, `run()` enters loop, `xHandleSubmit` prints mock echo
- Add `tui` subcommand to `main.cpp` with `--resume` flag
- Integration test: `a0 tui` shows split-panel, typing echos in scrollback

### Phase 3: Streaming LLM Integration

- Add `completeStreaming()` to `DeepSeekProvider` -- libcurl SSE parsing, token callback, cancel support
- Implement `processGoalStreaming()` on `AgentCore` with token/lifecycle callbacks
- Wire `xHandleSubmit` to call `processGoalStreaming` instead of mock echo
- Wire `xOnToken` -> `MessagePanel::streamUpdate()` via `Screen::Post(Task{})`
- Wire `xOnComplete` -> `MessagePanel::endStream()`
- Wire `xOnError` -> append error message, reset state
- Integration test: streaming response appears token-by-token in TUI

### Phase 4: Tool Execution Visibility

- Implement `xOnToolStart` -> `MessagePanel::appendToolCall(name, Pending, "")`
- Implement `xOnToolEnd` -> `MessagePanel::updateToolCall(index, Completed, output)`
- Add tool output formatting (stdout rendering, error highlighting, diff display)
- Wire `xHandleInterrupt` -> cancel SSE + kill tool subprocesses
- Integration test: tool call visible as collapsible block with status/output

### Phase 5: Markdown Rendering

- Implement `MarkdownRenderer` wrapping MD4C -- parse tree to FTXUI element tree
- Support: headings (h1-h4 bold/bright), emphasis (bold/italic), inline code, fenced code blocks (dim background), bullet/numbered lists, horizontal rules, links
- Wire into `MessagePanel` -- assistant messages rendered through MD4C before display
- Add `streaming=true` mode -- graceful handling of incomplete markdown
- Integration test: markdown-formatted response renders correctly

### Phase 6: Session Management

- Implement `SessionManager` wrapping `PersistenceStore` -- create/list/resume
- Implement `/sessions` command -- `DialogManager.show(session list)` -> onSelect -> resume
- Implement `/clear`, `/help`, `/quit` commands
- Implement `--resume <uuid>` CLI flag
- Integrate session persistence: each user-to-assistant turn appends to SQLite
- Integration test: `/sessions` shows past sessions, resume loads history

### Phase 7: Input History and Polish

- Implement input history file persistence (JSONL like opencode)
- Add `/export` command for JSONL session export
- Add transient status messages in StatusBar (`showStatus()`, auto-dismiss)
- Add Ctrl+L (redraw), auto-scroll behavior
- Add input placeholder cycling
- Handle terminal resize events via FTXUI's built-in resize handling

### Phase 8: Tests

- Unit tests for all classes (see Section 6.1)
- Integration tests with mock DeepSeek API (reuse existing mock server)
- E2E test: `echo "hello" | a0 tui` with piped input (non-interactive fallback)

---

## 9. Future Extensions

- **Permission prompts**: Modal dialog on tool execution showing tool name, args, diff. Approve/reject per tool. Requires SkillManager integration with permission callback.
- **Autocomplete**: `@file`, `/command` autocomplete in InputPanel (like opencode's autocomplete component)
- **Session sidebar**: Overlay sidebar showing session list, switch without `/sessions` command
- **Remote handoff**: When b1/c2 signals a remote operator, TUI enters "monitoring" mode -- displays actions but doesn't accept local input
- **Model switching**: `/model` command to switch DeepSeek model variants
- **Theme support**: Configurable color schemes via FTXUI styles
- **Split-view terminal**: Embed xterm.js-like PTY viewer in a TUI panel (challenging in terminal -- future investigation)
