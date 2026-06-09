# Technical Specification: Terminal UI (TUI) Sub-Module

## For a0 Agent — Version 3.1

---

## 1. Overview

### 1.1 Purpose

This document specifies a **TUI (Terminal User Interface) sub-module** for the existing a0 C++17 agent. The sub-module provides an interactive full-screen terminal interface replacing the basic stdin/stdout REPL, modelled after opencode.ai's terminal UI.

**Purpose**: The TUI sub-module enables rich interactive agent sessions directly in the terminal. Users see a split-panel layout with a scrollable message history on top and a persistent input area at the bottom. Agent responses stream token-by-token, tool executions are visible as collapsible status cards, and message roles are color-coded (user, assistant, system, tool). It is linked in-process into the `a0` binary and activated via the `a0 tui` subcommand. The TUI is the sole controller when active — no remote operator competes for input focus.

The TUI is a **thin rendering client** only. It does NOT own, hold pointers to, or directly interact with any core agent component:
- NO `DrivenCore` or `DrivenProvider` reference
- NO `LlmProvider` pointer
- NO `SkillManager` pointer or call
- NO `PersistenceStore` reference
- NO `SessionManager` class
- NO tool execution, no LLM interaction, no session management

All core agent functionality lives in `AppCoreThread` on a background thread. The TUI communicates with it exclusively through MPSC channels (defined in `src/shared/mpsc.h`).

**THIS BOUNDARY IS ENFORCED BY ARCHITECTURE. NO EXCEPTIONS.**

Any proposed change that introduces a direct reference from the TUI to any core component (`DrivenCore`, `DrivenProvider`, `LlmProvider`, `SkillManager`, `PersistenceStore`, `AgentCore`, `CommandRunner`, `DependencyGraph`, `ToolState`, `StreamRegistry`, `SessionContext`) must be rejected. The TUI is a **read-only display and input-forwarding layer** that:
- Sends `Command` variants through an MPSC `Sender<Command>`
- Receives `AppCoreEvent` variants from an MPSC `Receiver<AppCoreEvent>`
- Renders FTXUI elements
- Forwards user keystrokes and mouse events

### 1.2 Key Behaviors

- **Split-panel layout** — scrollable message panel (top, flex-grow) + fixed input bar (bottom)
- **Streaming responses** — LLM tokens arrive via MPSC `AppCoreEvent` channel, drained each frame by `drainEvents()`, posted to FTXUI's `Screen::Post(Task{})` for thread-safe re-render
- **Color-coded messages** — user (cyan), assistant (green), system (yellow), tool (blue), error (red)
- **Tool execution visibility** — collapsible blocks showing tool name, status spinner, stdout/stderr output, and diffs
- **Markdown rendering** — assistant messages rendered through MD4C into FTXUI elements (headings, bold/italic, code blocks, lists)
- **Input history** — Up/Down arrow navigation through previous prompts
- **Session management** — `/sessions` command sends `ListSessions` via MPSC; core responds with `SessionList` event
- **Ctrl+C interrupt** — sends `Cancel` command via MPSC
- **Status bar** — session UUID, agent state (idle/thinking/executing/error), b1 connection status, message count — all derived from MPSC events only
- **Copy-on-select** — mouse drag selects text, copies to clipboard via OSC 52 / xclip / wl-clipboard
- **Bracketed paste** — paste detection and multi-line input handling

### 1.3 Dependencies

- **FTXUI v6.1.9** — terminal UI framework (via FetchContent)
- **MD4C v0.5.2** — Markdown parser (via FetchContent or vendored)
- **`src/shared/mpsc.h`** — MPSC channel types (`Command`, `AppCoreEvent`)
- **`src/shared/trace.h`** — logging macros
- `b1` supervisor — optional; when b1 is running, status bar shows connection state (via injected `b1Status` function only)

**Non-dependencies (explicitly prohibited):**
- `src/driven_core.h` — NOT included by TUI
- `src/driven_provider.h` — NOT included by TUI
- `src/llm_provider.h` — NOT included by TUI
- `src/command_runner.h` — NOT included by TUI
- `src/skills/skills.h` — NOT included by TUI
- `src/persistence/persistence_store.h` — NOT included by TUI

---

## 2. Component Specifications (C++ Interfaces)

### 2.1 Boundary Enforcement Rules

These rules are checked by code review and MUST be verified on every change to TUI files:

1. **`src/tui/` files may NOT include** any header from `src/` outside of `src/tui/`, except:
   - `src/shared/mpsc.h` — for `Command` and `AppCoreEvent` types
   - `src/shared/trace.h` — for logging
   - `src/shared/resource_provider.h` — for `ResourceType` enum
   - FTXUI and MD4C headers
   - Standard library headers
2. **`src/tui/` files may NOT hold** raw pointers, references, or smart pointers to:
   - `DrivenCore`, `DrivenProvider`, `LlmProvider`, `DeepSeekProvider`
   - `SkillManager`, `SkillLoader`, `VersionManager`, `ValidationEngine`
   - `PersistenceStore`, `SqliteStore`, `ReplayEngine`
   - `CommandRunner`, `DependencyGraph`, `ToolState`
   - `ContainerManager`, `ComposeManager`, `DockerToolRunner`
3. **`src/tui/` files may NOT call** any method on any of the prohibited class types.
4. **`src/tui/` files may NOT create** `std::thread` objects for any purpose.
5. **`src/tui/` may NOT contain** `SessionManager` class or any equivalent.

Violation of these rules is a **design error** and must be corrected immediately, even if the code compiles and appears to work.

### 2.2 Core Data Structures

```cpp
#pragma once

#include <string>
#include <vector>
#include <cstdint>

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
    std::string toolArgs;           // arguments string for tool call
    ToolState toolState             = ToolState::Completed;
    std::string toolOutput;         // captured stdout/stderr
    std::string outputPreview;      // truncated preview from ToolEnd
    int64_t resourceId              = 0;   // for LoadResource expand
    int64_t resourceTotalBytes      = 0;   // total output size
    bool resourceExpanded           = false;
    bool resourceLoading            = false;
    int64_t timestamp               = 0;
    bool collapsed                  = true;
    bool streaming                  = false;
    int64_t sessionId               = 0;
    std::vector<MessageEntry> children;  // tool children under assistant
};

} // namespace a0::tui
```

### 2.3 MPSC Command/Event Types (referenced, defined in `src/shared/mpsc.h`)

```cpp
namespace a0::mpsc {

// --- Commands: TUI → AppCoreThread ---

struct SubmitGoal { std::string goal; };
struct Cancel {};
struct Shutdown {};
struct SetSession { int64_t sessionDbId; std::string sessionUuid; };
struct ListSessions { int limit = 20; };
struct ResumeSession { std::string uuid; };
struct LoadResource { ResourceType type; int64_t id; int64_t offset; int64_t limit; };

using Command = std::variant<SubmitGoal, Cancel, Shutdown, SetSession,
                             ListSessions, ResumeSession, LoadResource>;

// --- Events: AppCoreThread → TUI ---

struct LlmStart { int64_t streamId; int roundSeq; };
struct LlmChunk { int64_t streamId; int seq; std::string text; bool isFinal = false; };
struct LlmComplete { int64_t streamId; std::string finishReason; };
struct ToolStart { int64_t invocationId; std::string toolCallId; std::string toolName; std::string arguments; };
struct ToolChunk { int64_t invocationId; int seq; std::string text; std::string streamType; };
struct ToolEnd { int64_t invocationId; int exitCode = 0; int64_t totalBytes = 0; std::string outputPreview; };
struct Complete { int64_t sessionId; std::string summary; };
struct Error { std::string source; int64_t contextId = 0; std::string message; };
struct SessionMessage { std::string role; std::string content; std::string toolCallId; std::string name; std::string resultJson; int64_t createdAt = 0; };
struct SessionReady { int64_t dbId; std::string uuid; };
struct SessionList { struct Entry { std::string uuid; int64_t dbId = 0; std::string startedAt; int messageCount = 0; }; std::vector<Entry> entries; };
struct SessionHistory { int64_t dbId = 0; std::string uuid; bool found = false; std::vector<SessionMessage> messages; };
struct LoadResourceResult { int64_t id; std::string data; };

using AppCoreEvent = std::variant<LlmStart, LlmChunk, LlmComplete,
                                  ToolStart, ToolChunk, ToolEnd,
                                  Complete, Error,
                                  SessionReady, SessionList, SessionHistory,
                                  LoadResourceResult>;

} // namespace a0::mpsc
```

### 2.4 AgentTui (Facade)

```cpp
namespace a0::tui {

/// Main TUI orchestrator. Owns the FTXUI screen, loop, and all panels.
/// Communicates with the core agent via MPSC channels.
/// Does NOT own or reference any core component.
class AgentTui {
public:
    AgentTui(mpsc::Sender<mpsc::Command> cmdSender,
             mpsc::Receiver<mpsc::AppCoreEvent> evtReceiver,
             std::function<bool()> b1Status = nullptr,
             bool testMode = false);

    virtual ~AgentTui();

    int run();
    void shutdown();

    ftxui::Component component() const { return m_mainComponent; }

    void setScreen(ftxui::ScreenInteractive* screen);
    void clearScreen();
    ftxui::ScreenInteractive* screenPtr() const { return m_screen; }

    void submitInput(const std::string& input);
    void drainEvents();

    mpsc::Sender<mpsc::Command>& cmdSender() { return m_cmdSender; }

private:
    mpsc::Sender<mpsc::Command> m_cmdSender;
    mpsc::Receiver<mpsc::AppCoreEvent> m_evtReceiver;
    std::function<bool()> m_b1Status;

    std::unique_ptr<MessagePanel> m_messagePanel;
    std::unique_ptr<InputPanel> m_inputPanel;
    std::unique_ptr<StatusBar> m_statusBar;
    std::unique_ptr<DialogManager> m_dialogMgr;
    std::unique_ptr<MarkdownRenderer> m_markdown;

    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    AgentState m_agentState = AgentState::Idle;

    ftxui::ScreenInteractive* m_screen = nullptr;
    ftxui::Component m_mainComponent;

    bool m_mouseDown = false;
    bool m_mouseMoved = false;

    bool m_pasteActive = false;
    std::string m_pasteBuffer;
    int m_pasteCounter = 0;
    std::unordered_map<int, std::string> m_pastedContents;

    bool m_testMode = false;
    std::string m_streamingText;
    int m_assistantEntryIndex = -1;
    int64_t m_currentStreamId = 0;
    int m_currentRoundSeq = 0;
    bool m_hasActiveStream = false;

    struct ResourceCacheEntry {
        std::string data;
        int64_t timestamp;
        size_t size;
    };
    std::unordered_map<int64_t, ResourceCacheEntry> m_resourceCache;
    int64_t m_resourceCacheMaxBytes = 64 * 1024 * 1024;
    int64_t m_resourceCacheBytes = 0;

    struct PendingResourceReq {
        std::function<void(std::string)> callback;
    };
    std::unordered_map<int64_t, std::vector<PendingResourceReq>> m_pendingResourceReqs;

    std::string xExpandPastePlaceholders(const std::string& input);
    void xProcessPasteBuffer();

    void xBuildLayout();
    ftxui::Component xBuildMainContainer();

    void xHandleCoreEvent(const ::a0::mpsc::AppCoreEvent& ev);

    int xHandleSubmit(const std::string& input);
    int xHandleInterrupt();
    int xHandleCommand(const std::string& cmd);

    void xOnLlmStart(int64_t streamId, int roundSeq);
    void xOnLlmChunk(int64_t streamId, int seq, const std::string& text, bool isFinal);
    void xOnLlmComplete(int64_t streamId, const std::string& finishReason);
    void xOnToolStart(int64_t invocationId, const std::string& toolCallId,
                      const std::string& toolName, const std::string& arguments);
    void xOnToolChunk(int64_t invocationId, int seq, const std::string& text,
                      const std::string& streamType);
    void xOnToolEnd(int64_t invocationId, int exitCode, int64_t totalBytes,
                    const std::string& outputPreview);
    void xOnComplete(int64_t sessionId, const std::string& summary);
    void xOnError(const std::string& source, int64_t contextId, const std::string& message);
    void xOnSessionReady(int64_t dbId, const std::string& uuid);
    void xOnSessionList(const std::vector<mpsc::SessionList::Entry>& sessions);
    void xOnSessionHistory(int64_t dbId, const std::string& uuid,
                           const std::vector<mpsc::SessionMessage>& messages);
    void xOnLoadResourceResult(int64_t id, const std::string& data);

    void xEvictResourceCache();
    void xRequestResource(int64_t id, int64_t offset, int64_t limit,
                          std::function<void(std::string)> onData);

    int xCmdSessions();
    int xCmdHelp();
    int xCmdClear();
    int xCmdQuit();
};

} // namespace a0::tui
```

### 2.5 MessagePanel

```cpp
namespace a0::tui {

class MessagePanel {
public:
    MessagePanel();
    virtual ~MessagePanel();

    ftxui::Component component() const;

    int append(const MessageEntry& entry);
    void clear();

    int beginAssistant();
    int appendOrUpdateAssistantText(int asstIdx, const std::string& text);
    int endCurrentAssistantText(int asstIdx);
    int appendAssistantTool(int asstIdx, const std::string& name,
                            ToolState state, const std::string& args = "");
    int updateLastAssistantTool(int asstIdx, ToolState state,
                                const std::string& output);
    int updateLastAssistantToolOutput(int asstIdx, const std::string& text);
    int finalizeAssistant(int asstIdx);

    void scrollToBottom();
    int loadHistory(const std::vector<::a0::mpsc::SessionMessage>& messages);

    size_t count() const;
    void scrollUp(int n = 1);
    void scrollDown(int n = 1);
    void scrollToTop();
    int scrollTop() const;
    bool isAtBottom() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    ftxui::Element xRenderEntry(int entryIdx);
    ftxui::Element xRenderAssistant(int entryIdx);
    ftxui::Element xRenderToolBlock(const MessageEntry& entry) const;
    ftxui::Element xRenderStreamingPlaceholder(const MessageEntry& entry) const;
};

} // namespace a0::tui
```

### 2.6 InputPanel

```cpp
namespace a0::tui {

class InputPanel {
public:
    InputPanel();
    virtual ~InputPanel();

    ftxui::Component component() const;

    void setOnSubmit(std::function<void(const std::string&)> cb);
    void setOnInterrupt(std::function<void()> cb);
    void setOnChange(std::function<void(const std::string&)> cb);
    void insertText(const std::string& text);
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void setPlaceholder(const std::string& text);
    void clear();
    void focus();

    int addHistory(const std::string& prompt);
    int loadHistory(const std::string& path);
    int saveHistory(const std::string& path);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_enabled = true;

    static constexpr size_t MAX_HISTORY = 50;
};

} // namespace a0::tui
```

### 2.7 StatusBar

```cpp
namespace a0::tui {

class StatusBar {
public:
    StatusBar();
    virtual ~StatusBar();

    ftxui::Component component() const;

    void setSessionId(const std::string& uuid);
    void setAgentState(AgentState state);
    void setB1Connected(bool connected);
    void setMessageCount(size_t count);
    void showStatus(const std::string& msg, int timeoutSecs = 3);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

### 2.8 DialogManager

```cpp
namespace a0::tui {

class DialogManager {
public:
    DialogManager();
    virtual ~DialogManager();

    ftxui::Component component() const;

    void setMainComponent(ftxui::Component main);

    int show(ftxui::Component dialog, const std::string& title,
             std::function<void()> onDismiss = nullptr);

    void dismiss();
    void dismissAll();

    bool isActive() const;

    int showHelp();
    int showConfirm(const std::string& title,
                    const std::string& message,
                    std::function<void(bool)> onConfirm);

    int showList(const std::string& title,
                 const std::vector<std::pair<std::string, std::string>>& items,
                 std::function<void(const std::string&)> onSelect);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

### 2.9 MarkdownRenderer

```cpp
namespace a0::tui {

class MarkdownRenderer {
public:
    MarkdownRenderer();
    virtual ~MarkdownRenderer();

    ftxui::Element render(const std::string& md, bool streaming = false);
    ftxui::Element renderInline(const std::string& md);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::tui
```

### 2.10 Clipboard

Unchanged. See `src/tui/clipboard.spec.md` for full spec.

```cpp
namespace a0::tui {

void copyToClipboard(const std::string& text);

} // namespace a0::tui
```

**Fallback logic:**

1. Base64-encode the text
2. Write OSC 52 sequence: `ESC ] 52 ; c ; <base64> BEL`
3. If `WAYLAND_DISPLAY` is set → pipe to `wl-copy`
4. Else if `xclip` exists → pipe to `xclip -selection clipboard -i`

---

## 3. System Architecture

### 3.1 Thread Model

```
┌──────────────────────────────────────────────────────────────┐
│                    a0 Process (TUI Mode)                      │
│                                                              │
│  ┌─────────────────────────┐    ┌──────────────────────────┐ │
│  │  MAIN THREAD             │    │  BACKGROUND THREAD        │ │
│  │  (FTXUI Render Client)   │    │  (AppCoreThread)         │ │
│  │                          │    │                          │ │
│  │  AgentTui                │    │  DeepSeekProvider         │ │
│  │  ├─ MessagePanel         │    │  DrivenCore               │ │
│  │  ├─ InputPanel           │    │  │                        │ │
│  │  ├─ StatusBar            │    │  └─ SkillManager*         │ │
│  │  ├─ DialogManager        │    │  └─ PersistenceStore*     │ │
│  │  └─ MarkdownRenderer     │    │  └─ CommandRunner          │ │
│  │                          │    │  └─ DependencyGraph        │ │
│  │  NO core references      │    │                          │ │
│  │  NO SkillManager         │    │  * owned by AgentStack,   │ │
│  │  NO PersistenceStore     │    │    used read-only by core │ │
│  │  NO DrivenCore           │    │                          │ │
│  └──────────┬───────────────┘    └────────────┬─────────────┘ │
│             │                                 │               │
│             │  MPSC Channel (eventfd + mutex) │               │
│             │                                 │               │
│             │  Command variants →             │               │
│             │    SubmitGoal, Cancel,           │               │
│             │    SetSession, ListSessions,     │               │
│             │    ResumeSession, LoadResource   │               │
│             │                                 │               │
│             │  ← AppCoreEvent variants        │               │
│             │    LlmStart, LlmChunk,          │               │
│             │    LlmComplete, ToolStart,      │               │
│             │    ToolChunk, ToolEnd,          │               │
│             │    Complete, Error,             │               │
│             │    SessionReady, SessionList,   │               │
│             │    SessionHistory,              │               │
│             │    LoadResourceResult            │               │
│             │                                 │               │
└─────────────┼─────────────────────────────────┼───────────────┘
              │                                 │
              ▼                                 ▼
       User keyboard                    DeepSeek API
       b1 Supervisor                   SQLite DB (.a0/db/)
                                        Subprocess tools
```

### 3.2 C4 Diagram

```mermaid
graph TB
    subgraph "a0 Process — Main Thread (TUI Client)"
        TUI[TUI Sub-module]
        AT[AgentTui]
        MP[MessagePanel]
        IP[InputPanel]
        SB[StatusBar]
        DM[DialogManager]
        MR[MarkdownRenderer]
        CMD["m_cmdSender (MPSC Sender<Command>)"]
        EVT["m_evtReceiver (MPSC Receiver<AppCoreEvent>)"]
    end

    subgraph "a0 Process — Background Thread (AppCoreThread)"
        ACT[AppCoreThread]
        DC[DrivenCore]
        DP[DeepSeekProvider]
        CMDR["cmdReceiver (MPSC Receiver<Command>)"]
        EVTS["evtSender (MPSC Sender<AppCoreEvent>)"]
        SM[SkillManager]
        PS[PersistenceStore]
    end

    subgraph "External"
        LLM[DeepSeek API]
        B1[b1 Supervisor]
        USER((User))
    end

    subgraph "Storage"
        DB[(.a0/db/sessions.db)]
    end

    USER -->|keyboard input| TUI
    TUI --> AT
    AT --> MP
    AT --> IP
    AT --> SB
    AT --> DM
    AT --> MR

    AT -->|send| CMD
    EVT -->|drain| AT

    CMD -.->|MPSC channel| CMDR
    EVTS -.->|MPSC channel| EVT

    CMDR -->|dispatch| ACT
    ACT --> DC
    DC --> DP
    DP -->|curl_multi| LLM
    DC -->|executeTool| SM
    ACT -->|commands| EVTS

    ACT --> PS
    PS --> DB

    SM --> PS

    SB -.->|b1Status fn only| B1
```

**Caption**: The TUI and the agent core run on separate threads with zero shared state. All communication passes through MPSC channels: the TUI sends `Command` variants (SubmitGoal, Cancel, SetSession, ListSessions, ResumeSession, LoadResource), the core sends `AppCoreEvent` variants (LlmStart, LlmChunk, LlmComplete, ToolStart, ToolChunk, ToolEnd, Complete, Error, SessionReady, SessionList, SessionHistory, LoadResourceResult). The TUI never directly references SkillManager, PersistenceStore, DrivenCore, or any other core component.

---

## 4. Data Flow Diagrams

### 4.1 Full Interaction Cycle (User Input → MPSC → Event Display)

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant CMD as MPSC Sender
    participant EVT as MPSC Receiver
    participant ACT as AppCoreThread
    participant DC as DrivenCore
    participant DP as DeepSeekProvider
    participant SK as SkillManager
    participant MP as MessagePanel
    participant SB as StatusBar

    User->>IP: type "find log files"
    User->>IP: press Enter
    IP->>AT: onSubmit("find log files")
    AT->>SB: setAgentState(Thinking)
    AT->>MP: append(User, "find log files")
    AT->>CMD: send(SubmitGoal{"find log files"})

    Note over CMD,ACT: ═══ Thread boundary: MPSC channel ═══

    CMD-->>ACT: drain → SubmitGoal
    ACT->>DC: submitGoal("find log files")
    DC->>DP: startRequestStreaming(...)

    loop every ~16ms (FTXUI frame)
        AT->>EVT: drain()
        EVT-->>AT: [LlmChunk, ToolStart, ToolEnd, Complete, Error]

        rect rgb(230, 255, 230)
            Note over DP,MP: Background thread drives curl_multi
            ACT->>DC: tick()
            DC->>DP: tick()
            DP->>DP: curl_multi_perform()
            DP-->>DC: events
            DC-->>ACT: events
            ACT->>EVTS: send(LlmChunk, ToolStart, ToolEnd, ...)
        end

        AT->>AT: xHandleCoreEvent(event)

        alt LlmChunk
            AT->>MP: appendOrUpdateAssistantText("Found 3 files")
        else ToolStart
            AT->>MP: appendAssistantTool("glob", Running, args)
            AT->>SB: setAgentState(Executing)
        else ToolEnd
            AT->>MP: updateLastAssistantTool(idx, Completed, output)
            AT->>SB: setAgentState(Thinking)
        else Complete
            AT->>MP: finalizeAssistant(idx)
            AT->>SB: setAgentState(Idle)
            AT->>IP: setEnabled(true)
            AT->>IP: focus()
        end
    end
```

### 4.2 Session List and Resume (via MPSC)

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant CMD as MPSC Sender
    participant EVT as MPSC Receiver
    participant ACT as AppCoreThread
    participant PS as PersistenceStore
    participant DM as DialogManager
    participant MP as MessagePanel
    participant SB as StatusBar

    User->>IP: type "/sessions"
    IP->>AT: onSubmit("/sessions")
    AT->>AT: xHandleCommand("/sessions")
    AT->>CMD: send(ListSessions{limit: 20})

    Note over CMD,ACT: ═══ Thread boundary ═══

    CMD-->>ACT: drain → ListSessions
    ACT->>PS: loadSessions(20)
    PS-->>ACT: [SessionInfo...]
    ACT->>EVTS: send(SessionList{entries})

    Note over ACT,EVT: ═══ Thread boundary ═══

    EVT-->>AT: drain → SessionList
    AT->>AT: xOnSessionList(sessions)
    AT->>DM: showList("Sessions", items, onSelect)
    DM-->>User: (renders selectable list)

    User->>DM: select session "abc-123"
    DM->>AT: onSelect("abc-123")
    AT->>CMD: send(ResumeSession{uuid:"abc-123"})

    Note over CMD,ACT: ═══ Thread boundary ═══

    CMD-->>ACT: drain → ResumeSession
    ACT->>PS: findSessionByUuid("abc-123")
    ACT->>PS: loadMessages(...)
    PS-->>ACT: session messages
    ACT->>DC: setSession(...)
    ACT->>EVTS: send(SessionHistory{dbId, uuid, found:true, messages})

    Note over ACT,EVT: ═══ Thread boundary ═══

    EVT-->>AT: drain → SessionHistory
    AT->>AT: xOnSessionHistory(dbId, uuid, messages)
    AT->>MP: clear()
    AT->>MP: loadHistory(messages)
    AT->>SB: setSessionId("abc-123")
    AT->>SB: setMessageCount(N)
    DM->>AT: dismiss()
```

### 4.3 Interrupt Handling (via MPSC)

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant CMD as MPSC Sender
    participant ACT as AppCoreThread

    Note over AT: Streaming in progress

    User->>IP: Ctrl+C
    IP->>AT: onInterrupt()
    AT->>CMD: send(Cancel{})

    Note over CMD,ACT: ═══ Thread boundary ═══

    CMD-->>ACT: drain → Cancel
    ACT->>ACT: core.cancel()

    AT->>SB: setAgentState(Idle)
    AT->>AT: showStatus("Interrupted", 3)
    AT->>IP: setEnabled(true)
    AT->>IP: focus()
```

### 4.4 Startup Sequence (main.cpp → Thread Creation)

```mermaid
sequenceDiagram
    participant MAIN as main()
    participant AS as AgentStack
    participant CMD as MPSC Sender
    participant EVT as MPSC Receiver
    participant ACT as AppCoreThread
    participant TUI as AgentTui

    MAIN->>AS: init (persistence, skillMgr, runners, handlers)
    AS->>AS: skillMgr.loadAll()
    MAIN->>AS: registerAgent, createSession
    MAIN->>AS: setRecordingSession
    MAIN->>AS: SessionContext::init()
    MAIN->>AS: launch b1, connect socket

    Note over MAIN: Create MPSC channels
    MAIN->>MAIN: create cmdChan, evtChan

    Note over MAIN: Start core on background thread
    MAIN->>ACT: start(cmdRcvr, evtSender, wakeupFn)
    ACT-->>ACT: (thread starts, ppoll loop begins)

    Note over MAIN: Send session to core
    MAIN->>CMD: send(SetSession{dbId, sid})
    CMD-->>ACT: drain → SetSession
    ACT->>ACT: core.setSession(dbId, sid)
    ACT->>EVTS: send(SessionReady{dbId, sid})

    Note over MAIN: Create TUI (NO core refs)
    MAIN->>TUI: AgentTui(cmdSender, evtReceiver, b1StatusFn)

    opt --resume flag
        MAIN->>CMD: send(ResumeSession{uuid})
        CMD-->>ACT: drain → ResumeSession
        ACT-->>EVTS: send(SessionHistory{...})
        EVT-->>TUI: (drained on first frame)
    end

    MAIN->>TUI: run()
    TUI-->>TUI: FTXUI renders
```

### 4.5 AgentTui::run() — Event Loop

```mermaid
sequenceDiagram
    participant TUI as AgentTui
    participant EVT as MPSC Receiver
    participant f as FTXUI Screen

    Note over TUI: No xTickCore(). No DrivenCore::tick().
    Note over TUI: All core events arrive via MPSC.

    loop every ~16ms
        TUI->>f: Loop::RunOnce() → FTXUI renders one frame
        f->>TUI: (user input processed)

        TUI->>EVT: drain()
        alt events available
            EVT-->>TUI: vector<AppCoreEvent>
            TUI->>TUI: xHandleCoreEvent(ev)

            opt not idle
                TUI->>f: RequestAnimationFrame()
            end
        end

        TUI->>TUI: sleep_for(16ms)
    end
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
| `--no-permissions` | `false` | Auto-approve tool execution |

### 5.2 In-TUI Commands

| Command | Description |
|---------|-------------|
| `/sessions` or `/s` | Send ListSessions via MPSC → display response dialog |
| `/help` or `/h` | Show keybinding reference |
| `/clear` or `/cls` | Clear message panel (display only) |
| `/quit` or `/q` or `:q` | Send Shutdown via MPSC, exit TUI |

### 5.3 Keybindings

| Key | Action |
|-----|--------|
| `Enter` | Submit input |
| `Shift+Enter` | Newline in input |
| `Up/Down` | Navigate input history |
| `Ctrl+C` | Send Cancel via MPSC |
| `Ctrl+Q` | (reserved — `/quit` used instead) |
| `Ctrl+L` | Redraw screen |
| `PageUp/PageDown` | Scroll message panel |
| `Home/End` | Scroll to top/bottom |
| `Mouse wheel` | Scroll message panel |
| `Mouse drag` | Select text → copy to clipboard |
| `Escape` | Close dialog / cancel |
| `Click on tool block` | Toggle collapse/expand |

### 5.4 Environment Variables

| Variable | Used by | Description |
|----------|---------|-------------|
| `A0_TUI_NO_PERMISSIONS` | AgentTui | Skip permission prompts (like `--no-permissions`) |

---

## 6. Testing Requirements

### 6.1 Unit Tests

| Class | Test Case | Verification |
|-------|-----------|-------------|
| `AgentTui` | Construct with MPSC channels | No crash, component() returns non-null |
| `AgentTui` | `submitInput` sends SubmitGoal | `cmdSender` queue contains SubmitGoal |
| `AgentTui` | `drainEvents` with LlmChunk | Chunk text appended to message panel |
| `AgentTui` | `drainEvents` with ToolStart | Tool block created in message panel |
| `AgentTui` | `drainEvents` with ToolEnd | Tool block updated with output |
| `AgentTui` | `drainEvents` with Complete | Streaming ended, state idle |
| `AgentTui` | `drainEvents` with Error | Error message appended |
| `AgentTui` | `drainEvents` with SessionReady | Session ID set in status bar |
| `AgentTui` | `drainEvents` with SessionList | Dialog shows session list |
| `AgentTui` | `drainEvents` with SessionHistory | History loaded into message panel |
| `AgentTui` | `/quit` sends Shutdown | cmdSender receives Shutdown |
| `AgentTui` | Ctrl+C sends Cancel | cmdSender receives Cancel |
| `AgentTui` | `/sessions` sends ListSessions | cmdSender receives ListSessions |
| `MessagePanel` | `append` User message | Renders cyan-colored text |
| `MessagePanel` | `append` Assistant message | Renders green-colored text |
| `MessagePanel` | `beginAssistant` + `appendOrUpdateAssistantText` | Entry created, updated text rendered |
| `MessagePanel` | `finalizeAssistant` | Streaming indicator removed, final text rendered |
| `MessagePanel` | `appendAssistantTool` Running → Completed | Spinner stops, output displayed |
| `MessagePanel` | `loadHistory` with 10 messages | All 10 rendered in order |
| `MessagePanel` | `clear` | Count = 0 |
| `MessagePanel` | `scrollUp`/`scrollDown` | Focus index changes correctly |
| `InputPanel` | `setOnSubmit` callback fires | Callback invoked with input text |
| `InputPanel` | `addHistory` + Up arrow | Previous input restored |
| `InputPanel` | `setEnabled(false)` | Input not accepting text |
| `InputPanel` | `insertText` + programmatic | contentBuf updated |
| `StatusBar` | `setAgentState` all states | Correct label displayed for each |
| `StatusBar` | `setB1Connected` | Shows connected/disconnected indicator |
| `StatusBar` | `showStatus` with timeout | Flash message appears then reverts |
| `DialogManager` | `show` + `dismiss` | Dialog appears then disappears |
| `DialogManager` | `showConfirm` true path | onConfirm(true) called |
| `DialogManager` | `showList` select | onSelect called with selected value |
| `MarkdownRenderer` | `render` with heading | Bold large text element |
| `MarkdownRenderer` | `render` with code block | Dim background element |
| `MarkdownRenderer` | `render` with bold/italic | Correct decorators applied |
| `MarkdownRenderer` | `renderInline` | No block-level elements |
| `MarkdownRenderer` | `render` with incomplete markdown | Graceful handling (no crash) |
| `Clipboard` | Empty text | No output, no process spawned |
| `Clipboard` | Text copied via OSC 52 | OSC 52 sequence written to stdout |
| `Clipboard` | Base64 output correctness | Decoded string matches input |
| `Clipboard` | Wayland fallback | `wl-copy` invoked when `WAYLAND_DISPLAY` set |
| `Clipboard` | X11 fallback | `xclip` invoked when no Wayland |
| **MPSC** | `SubmitGoal` through channel | Eventfd readable, drain returns command |
| **MPSC** | `SessionList` through channel | Eventfd readable, drain returns event |
| **MPSC** | Multiple senders | No data race, all messages received |

### 6.2 Integration Tests

| ID | Scenario | Steps | Expected |
|----|----------|-------|----------|
| INT‑TUI‑01 | TUI launches | Run `a0 tui` | FTXUI screen renders, status bar shows "Idle" |
| INT‑TUI‑02 | Submit a goal | Type "hello", press Enter | User message appears, agent responds via MPSC |
| INT‑TUI‑03 | Streaming display | Send goal that triggers long response | Tokens appear incrementally in message panel |
| INT‑TUI‑04 | Tool execution visible | Send goal that triggers tool call | Tool block appears with name, status, output |
| INT‑TUI‑05 | Interrupt during streaming | Ctrl+C while agent is responding | Cancel sent via MPSC, streaming stops, "Interrupted" shown |
| INT‑TUI‑06 | `/sessions` command | Type `/sessions` | ListSessions sent via MPSC, dialog shows session list |
| INT‑TUI‑07 | Resume session | Select a session from `/sessions` | ResumeSession sent via MPSC, history loaded |
| INT‑TUI‑08 | Input history | Submit 3 prompts, press Up 3 times | All 3 prompts accessible in reverse order |
| INT‑TUI‑09 | `/help` command | Type `/help` | Help dialog with keybindings |
| INT‑TUI‑10 | `/quit` | Type `/quit` | TUI exits cleanly, terminal restored |
| INT‑TUI‑11 | Resume via flag | `a0 tui --resume <uuid>` | ResumeSession sent via MPSC before FTXUI loop |
| INT‑TUI‑12 | b1 status indicator | Start b1, launch TUI | Status bar shows "b1: ✓" |
| INT‑TUI‑13 | AppCoreThread start | TUI launches, core thread starts | SessionReady event received, status bar shows session UUID |
| INT‑TUI‑14 | Session list/display | Two sessions exist, list them | SessionList event received, both sessions shown in dialog |

### 6.3 Thread Sanitizer (TSAN) Tests

| Test | What to check |
|------|---------------|
| MPSC SubmitGoal + event drain | No data races on shared deque |
| MPSC multiple AppCoreEvent types | All variant types correctly transmitted |
| AgentTui drainEvents during render | No race between event processing and FTXUI state |

---

## 7. Integration with Existing Main Specification

### 7.1 `main.cpp` — TUI Path

The `cmdTui` function is restructured. The key change: `AgentTui` no longer receives any core component references. Instead, `AppCoreThread` is created and started on a background thread, and MPSC channels bridge the two.

```cpp
void cmdTui(...) {
    // --- Phase 1: Bootstrap (single-threaded) ---
    AgentStack stack(a0Dir, skillsDir, apiKey, mockUrl, ...);
    stack.skillMgr.loadAll();
    int agentId = xRegisterAgent(stack.persistence);
    std::string sid = ...;
    int64_t sessionDbId = stack.persistence.createSession(sid, 0, 0, agentId);
    stack.skillMgr.setRecordingSession(sessionDbId);

    // SessionContext init
    SessionContext sessionCtx(initialCwd, a0Dir, sid, sessionDbId, &stack.persistence);
    sessionCtx.init(&stack.skillMgr);

    // Launch b1, connect
    int b1Fd = xLaunchB1(a0Dir, sid, initialCwd);
    std::function<bool()> b1Status = [&b1Fd]() { return b1Fd >= 0; };

    // --- Phase 2: Create threads ---
    auto [cmdSender, cmdRcvr] = mpsc::Channel<mpsc::Command>::create();
    auto [evtSender, evtRcvr] = mpsc::Channel<mpsc::AppCoreEvent>::create();

    AppCoreThread coreThread(apiKey, model, &stack.skillMgr, &stack.persistence);
    if (!mockUrl.empty()) coreThread.setMockUrl(mockUrl);
    coreThread.start(std::move(cmdRcvr), std::move(evtSender),
                     /* wakeupFn */ [screen = ...]() { ... } );

    cmdSender.send(mpsc::SetSession{sessionDbId, sid});

    // --- Phase 3: Create TUI (zero core references) ---
    AgentTui tui(std::move(cmdSender), std::move(evtReceiver), b1Status);

    // Resume if requested
    if (!tuiResumeUuid.empty())
        tui.cmdSender().send(mpsc::ResumeSession{tuiResumeUuid});

    int rc = tui.run(tuiTestMode);

    // --- Phase 4: Cleanup ---
    tui.cmdSender().send(mpsc::Shutdown{});
    coreThread.stop();
    return rc;
}
```

### 7.2 `main.cpp` — Run Path (Headless)

The `cmdRun` path is also updated to use `AppCoreThread` for consistency. Instead of `DrivenCore::runSync()` on the main thread, it creates `AppCoreThread`, sends `SetSession` + `SubmitGoal`, waits for `Complete`/`Error` event, prints result, and shuts down:

```cpp
static int cmdRun(...) {
    // ... bootstrap (same as TUI path) ...
    AgentStack stack(...);
    stack.skillMgr.loadAll();
    // ... session creation, SessionContext::init() ...

    auto [cmdSender, cmdRcvr] = mpsc::Channel<mpsc::Command>::create();
    auto [evtSender, evtRcvr] = mpsc::Channel<mpsc::AppCoreEvent>::create();

    AppCoreThread coreThread(apiKey, model, &stack.skillMgr, &stack.persistence);
    coreThread.start(std::move(cmdRcvr), std::move(evtSender));
    cmdSender.send(mpsc::SetSession{sessionDbId, sid});
    cmdSender.send(mpsc::SubmitGoal{runPrompt});

    // Poll for completion
    std::string result;
    while (true) {
        auto events = evtRcvr.drain();
        for (auto& ev : events) {
            if (auto* c = std::get_if<mpsc::Complete>(&ev)) result = c->summary;
            else if (auto* e = std::get_if<mpsc::Error>(&ev)) result = "ERROR: " + e->message;
        }
        if (!result.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    cmdSender.send(mpsc::Shutdown{});
    coreThread.stop();
    // ... print result ...
}
```

### 7.3 AppCoreThread (Background Core)

`AppCoreThread` (defined in `src/app_core_thread.h/.cpp`) is the sole owner of the agent core on a background thread:

- Constructed with `apiKey`, `model`, `SkillManager*`, `PersistenceStore*`
- Creates `DeepSeekProvider` + `DrivenCore` as stack locals in `xRun()`
- Main loop: `ppoll()` on wakeup eventfd + command MPSC fd
- Drains commands: `SetSession` → `core.setSession()`, `SubmitGoal` → `core.submitGoal()`, etc.
- Ticks `DrivenCore::tick()` each iteration, sends events via `evtSender`
- Blocks SIGCHLD, drains via `waitpid(WNOHANG)` for tool subprocess reaping
- Responds to `ListSessions` by querying `PersistenceStore::loadSessions()` directly
- Responds to `ResumeSession` by querying `findSessionByUuid()`, `loadMessages()`, sets core session, sends `SessionHistory` event

The `AppCoreThread` API:

```cpp
class AppCoreThread {
public:
    AppCoreThread(const std::string& apiKey, const std::string& model,
                  a0::skills::SkillManager* skillMgr,
                  a0::persistence::PersistenceStore* persistence);
    ~AppCoreThread();

    void start(mpsc::Receiver<mpsc::Command> cmdRcvr,
               mpsc::Sender<mpsc::AppCoreEvent> evtSender,
               std::function<void()> wakeupFn = nullptr);
    void stop();
    bool running() const;

    void setMockUrl(const std::string& url);
};
```

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
    markdown_renderer.cpp
    styles.cpp
    clipboard.cpp
)
target_include_directories(tui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tui_lib PUBLIC
    ftxui::ftxui
    md4c::md4c
    nlohmann_json::nlohmann_json
)
```

Note: `tui_lib` does NOT link `a0_lib` or `persistence_lib` — it has no dependency on any core library. The MPSC header (`src/shared/mpsc.h`) is header-only and included directly.

Root `CMakeLists.txt` adds `add_subdirectory(src/tui)` and links `tui_lib` and `app_core_thread.cpp` to the `a0` executable.

### 7.5 Project File Layout

```
src/tui/
├── CMakeLists.txt              # FTXUI + MD4C deps, tui_lib build (NO core deps)
├── technical-specification.md  # this document
├── agent_tui.spec.md           # AgentTui component spec
├── dialog_manager.spec.md      # DialogManager component spec
├── input_panel.spec.md         # InputPanel component spec
├── markdown_renderer.spec.md   # MarkdownRenderer component spec
├── message_panel.spec.md       # MessagePanel component spec
├── status_bar.spec.md          # StatusBar component spec
├── styles.spec.md              # Styles enum/constant spec
├── clipboard.spec.md           # Clipboard utility spec
├── styles.h                    # Color/decorator constants
├── styles.cpp
├── agent_tui.h                 # Facade (MPSC sender + receiver only)
├── agent_tui.cpp
├── message_panel.h             # Scrollable message display
├── message_panel.cpp
├── input_panel.h               # Input bar with history
├── input_panel.cpp
├── status_bar.h                # Session/agent status bar
├── status_bar.cpp
├── dialog_manager.h            # Modal dialog system
├── dialog_manager.cpp
├── markdown_renderer.h         # MD4C -> FTXUI element
├── markdown_renderer.cpp
├── clipboard.h                 # OSC 52 / xclip / wl-clipboard
├── clipboard.cpp
```

### 7.6 `specs/concurrency-model.md` Impact

The concurrency model spec is updated:
- **C1** (FTXUI Event Loop): Renamed from "Active" to "TUI Render Client" — no core access, only MPSC drain
- **C2** (AppCoreThread): Changed from "Designed, not wired" to **"Active"** — core agent on background thread
- **C4** (Skill Executor Thread): Removed — `skill_runner.cpp` no longer compiled
- New event types added to the MPSC protocol section (LlmStart, LlmChunk, LlmComplete, ToolChunk, LoadResource, LoadResourceResult)

---

## Appendix A: Boundary Enforcement Checklist

Every pull request touching `src/tui/` MUST pass this checklist:

| Check | Rule |
|-------|------|
| `agent_tui.h` includes | No `#include` of any `src/` header outside `src/tui/`, `src/shared/mpsc.h`, `src/shared/trace.h`, `src/shared/resource_provider.h` |
| `agent_tui.cpp` includes | Same rule |
| All `src/tui/*` includes | Same rule |
| `agent_tui.h` members | No pointer or reference to DrivenCore, LlmProvider, SkillManager, PersistenceStore, SessionManager |
| `agent_tui.cpp` members | No direct call to any core API |
| `agent_tui.cpp` session ops | No direct persistence calls. `/sessions` → MPSC only. |
| All MPSC event handling | Read-only. Events are received and rendered, never modified and sent back. |
| `session_manager.h/.cpp` | Do not exist. |
| `src/tui/CMakeLists.txt` | Does NOT link `a0_lib` or `persistence_lib`. |
