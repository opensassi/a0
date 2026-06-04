# agent_tui.h/.cpp — TUI Facade

## 1. Overview

The `AgentTui` class is the facade for the TUI sub-module. It owns all panels (MessagePanel, InputPanel, StatusBar, DialogManager, SessionManager), constructs the FTXUI component hierarchy, manages the event loop via `ScreenInteractive`, and wires streaming callbacks from AgentCore/DeepSeekProvider into the rendering pipeline. It is the sole entry point for the `a0 tui` subcommand.

**Lifecycle:** Construct → (optional `resumeSession`) → `run()` (blocks) → cleanup → destruct.

**Depends on**: All other TUI components, `a0::AgentCore`, `a0::persistence::PersistenceStore`, `a0::skills::SkillManager`, FTXUI `ScreenInteractive`, `Loop`, `Component`

---

## 2. Component Specifications

```cpp
namespace a0::tui {

/// Main TUI orchestrator. Owns the FTXUI screen, loop, and all panels.
/// Constructed with references to agent components, then run() enters the event loop.
class AgentTui {
public:
    /// \param core         AgentCore for processGoalStreaming.
    /// \param persistence  PersistenceStore for session list/resume.
    /// \param skills       SkillManager for tool schema display (future).
    /// \param b1Status     Function to query b1 connection status (optional).
    /// \param noPermissions If true, auto-approve tool execution.
    AgentTui(::a0::AgentCore* core,
             ::a0::persistence::PersistenceStore* persistence,
             ::a0::skills::SkillManager* skills,
             std::function<bool()> b1Status = nullptr,
             bool noPermissions = false);

    virtual ~AgentTui();

    /// Enter the FTXUI event loop. Blocks until user quits (Ctrl+Q / :q / /quit).
    /// During the loop:
    ///   - Handles user input via InputPanel callbacks
    ///   - Dispatches streaming responses to MessagePanel
    ///   - Updates StatusBar on state changes
    ///   - Processes dialog interactions
    /// \retval 0  Normal exit.
    /// \retval -1 FTXUI error.
    int run();

    /// Request graceful shutdown. Posts quit task to FTXUI event loop.
    void shutdown();

    /// Resume an existing session by UUID.
    /// Loads messages from persistence into the message panel.
    /// \retval 0  Session loaded.
    /// \retval -1 Session not found.
    int resumeSession(const std::string& uuid);

    /// Get current session UUID.
    std::string currentSessionId() const;

private:
    // Non-owning dependencies
    ::a0::AgentCore* m_core;
    ::a0::persistence::PersistenceStore* m_persistence;
    ::a0::skills::SkillManager* m_skills;
    std::function<bool()> m_b1Status;
    bool m_noPermissions;

    // Owned panels
    std::unique_ptr<MessagePanel> m_messagePanel;
    std::unique_ptr<InputPanel> m_inputPanel;
    std::unique_ptr<StatusBar> m_statusBar;
    std::unique_ptr<DialogManager> m_dialogMgr;
    std::unique_ptr<SessionManager> m_sessionMgr;

    // Session state
    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    AgentState m_agentState = AgentState::Idle;
    bool m_streaming = false;
    int m_streamingEntryIndex = -1;

    // FTXUI objects
    ftxui::ScreenInteractive* m_screen;     // non-owning (created on heap by run)
    ftxui::Component m_mainComponent;        // owned by ScreenInteractive

    // Layout
    int xBuildLayout();
    ftxui::Component xBuildMainContainer();

    // Input dispatch
    int xHandleSubmit(const std::string& input);
    int xHandleInterrupt();
    int xHandleCommand(const std::string& cmd);

    // Goal processing
    int xProcessGoal(const std::string& goal);

    // Streaming callbacks — called from background threads (DeepSeek SSE parser)
    // All callbacks must post to Screen via Post(Task{...}) for thread safety.
    void xOnToken(const std::string& token);
    void xOnToolStart(const std::string& name, const nlohmann::json& params);
    void xOnToolEnd(const std::string& name, const std::string& output, bool success);
    void xOnComplete(const std::string& fullOutput);
    void xOnError(const std::string& error);
    void xOnInterrupted();

    // Internal commands
    int xCmdSessions();
    int xCmdHelp();
    int xCmdClear();
    int xCmdQuit();
    int xCmdExport();
};

} // namespace a0::tui
```

---

## 3. Architecture

```mermaid
graph TB
    subgraph "AgentTui (Facade)"
        AT[AgentTui]
    end

    subgraph "Panels"
        MP[MessagePanel]
        IP[InputPanel]
        SB[StatusBar]
        DM[DialogManager]
        SM[SessionManager]
        MR[MarkdownRenderer]
    end

    subgraph "Agent Core (in-process)"
        Core[AgentCore]
        DP[DeepSeekProvider]
        SR[SkillRunner]
        PS[PersistenceStore]
        SK[SkillManager]
    end

    subgraph "FTXUI"
        Screen[ScreenInteractive]
        Loop[Loop]
        Layout[Component Tree]
    end

    subgraph "External"
        LLM[DeepSeek API]
        B1[b1 Supervisor]
        DB[(SQLite)]
    end

    AT --> Screen
    AT --> Loop
    AT --> Layout
    Screen -->|render| Loop

    AT --> MP
    AT --> IP
    AT --> SB
    AT --> DM
    AT --> SM
    AT --> MR

    AT -->|processGoalStreaming| Core
    Core --> DP
    Core --> SR
    Core --> SK
    Core --> PS

    DP -->|SSE tokens| LLM
    DP --> AT
    SR -->|tool callbacks| AT

    SM --> PS
    PS --> DB

    SB -.->|b1 status| B1
```

---

## 4. Data Flow

### 4.1 Startup and Main Loop

```mermaid
sequenceDiagram
    participant main as main.cpp
    participant AT as AgentTui
    participant FTXUI as ScreenInteractive
    participant SM as SessionManager
    participant PS as PersistenceStore

    main->>AT: AgentTui(core, persistence, skills, ...)
    alt --resume <uuid>
        main->>AT: resumeSession("abc-123")
        AT->>SM: resume("abc-123", outDbId)
        SM->>PS: findSessionByUuid("abc-123")
        PS-->>SM: 42
        SM-->>AT: 0
        AT->>PS: loadMessages(42)
        PS-->>AT: [Message...]
        AT->>AT: m_messagePanel->loadHistory(...)
    end
    main->>AT: run()
    AT->>FTXUI: ScreenInteractive::Fullscreen()
    AT->>AT: xBuildLayout()
    AT->>FTXUI: Loop(&screen, mainComponent)
    AT->>FTXUI: loop.Run()  -- blocks until quit
    Note over FTXUI: User types, streams, etc. via callbacks
    AT-->>main: 0
```

### 4.2 Submit → Stream → Complete

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant SB as StatusBar
    participant MP as MessagePanel
    participant Core as AgentCore
    participant DP as DeepSeekProvider
    participant SR as SkillRunner
    participant FTXUI as Screen

    User->>IP: type & submit
    IP->>AT: xHandleSubmit("find log files")

    AT->>AT: xHandleCommand or xProcessGoal

    AT->>SM: create(uuid)
    AT->>SB: setSessionId(uuid)
    AT->>SB: setAgentState(Thinking)
    AT->>MP: append(User, "find log files")
    AT->>MP: beginStreaming(Assistant)
    AT->>SB: setMessageCount(N)
    AT->>IP: setEnabled(false)

    AT->>Core: processGoalStreaming(goal, onToken, onToolStart, onToolEnd, onComplete, onError)

    Core->>DP: completeStreaming(messages, schemas, onToken)
    DP->>DP: parse SSE stream

    loop each token from DeepSeek
        DP->>AT: xOnToken("I'll")
        AT->>FTXUI: Post(Task{...})
        FTXUI->>MP: streamUpdate(idx, "I'll")
        MP->>FTXUI: (re-render)

        DP->>AT: xOnToken(" search")
        AT->>FTXUI: Post(Task{...})
        FTXUI->>MP: streamUpdate(idx, "I'll search")
    end

    rect rgb(255, 240, 230)
        Note right of Core: Tool call
        SR->>AT: xOnToolStart("glob", {pattern:"*.log"})
        AT->>FTXUI: Post(Task{...})
        FTXUI->>MP: appendToolCall("glob", Pending)
        FTXUI->>SB: setAgentState(Executing)

        SR->>AT: xOnToolEnd("glob", "3 files", true)
        AT->>FTXUI: Post(Task{...})
        FTXUI->>MP: updateToolCall(idx, Completed, "3 files")
        FTXUI->>SB: setAgentState(Thinking)
    end

    DP->>AT: xOnComplete("Found 3 log files matching *.log")
    AT->>FTXUI: Post(Task{...})
    FTXUI->>MP: endStream(idx)
    FTXUI->>SB: setAgentState(Idle)
    FTXUI->>SB: setMessageCount(N+1)

    AT->>IP: setEnabled(true)
    AT->>IP: clear()
    AT->>IP: focus()
```

### 4.3 Interrupt

```mermaid
sequenceDiagram
    participant User
    participant IP as InputPanel
    participant AT as AgentTui
    participant DP as DeepSeekProvider
    participant SR as SkillRunner
    participant MP as MessagePanel
    participant SB as StatusBar

    Note over AT: Streaming in progress
    User->>IP: Ctrl+C
    IP->>AT: xHandleInterrupt()
    AT->>DP: cancel()
    AT->>SR: cancel running tool
    AT->>MP: append(System, "Interrupted")
    AT->>SB: setAgentState(Idle)
    AT->>IP: setEnabled(true)
    AT->>IP: focus()
```

---

## 5. D3 Animation

```html
<!DOCTYPE html>
<html>
<head>
<style>
body { font-family: monospace; background: #1a1a2e; color: #ccc; padding: 24px; }
.terminal { border: 1px solid #444; border-radius: 6px; max-width: 800px; overflow: hidden; }
.statusbar { display: flex; background: #2d2d44; padding: 4px 12px; font-size: 12px; border-bottom: 1px solid #444; }
.status-item { margin-right: 12px; }
.session { color: #888; }
.state { color: #ffea00; }
.b1 { color: #00e676; margin-left: auto; }
.count { color: #888; }
.scrollback { padding: 8px 12px; min-height: 200px; max-height: 300px; overflow-y: auto; background: #1a1a2e; }
.msg { margin: 4px 0; padding: 4px 8px; border-radius: 3px; }
.user { color: #00bcd4; border-left: 2px solid #00bcd4; }
.assistant { color: #ddd; border-left: 2px solid #00e676; }
.tool { color: #448aff; border-left: 2px solid #448aff; font-size: 12px; background: #1a1a3e; }
.system { color: #ffea00; border-left: 2px solid #ffea00; font-size: 12px; }
.cursor { animation: blink 1s step-end infinite; }
@keyframes blink { 50% { opacity: 0; } }
.inputbar { display: flex; border-top: 1px solid #444; background: #2d2d44; }
.prompt { padding: 8px 0 8px 12px; color: #888; }
.input { flex: 1; background: transparent; border: none; color: #eee; padding: 8px; font-family: monospace; outline: none; }
.hint { padding: 8px 12px; color: #555; font-size: 12px; }
button { margin-top: 16px; margin-right: 8px; }
</style>
</head>
<body>
<h3>agent_tui — Full Interaction Demo</h3>
<div class="terminal" id="terminal">
  <div class="statusbar">
    <span class="status-item session" id="session">📋 abc-123</span>
    <span class="status-item state" id="state">💤 Idle</span>
    <span class="status-item b1">b1: ✓</span>
    <span class="status-item count" id="count">3 msgs</span>
  </div>
  <div class="scrollback" id="scrollback">
    <div class="msg user">> find log files</div>
    <div class="msg assistant" id="streamMsg">I will search for log files...</div>
    <div class="msg tool">🔧 glob <span id="toolStatus">⏳ running</span></div>
    <div class="msg tool" style="display:none;" id="toolResult">📄 found 3 files</div>
  </div>
  <div class="inputbar">
    <span class="prompt">></span>
    <input class="input" id="input" value="" placeholder="Type a message..." readonly/>
    <span class="hint">Enter</span>
  </div>
</div>
<div id="toast"></div>
<button onclick="simulateStep()" data-testid="play-pause">Step</button>
<button onclick="resetDemo()">Reset</button>

<script>
let step = 0;
const tokens = ['I will use ', 'the glob tool ', 'to search ', 'for *.log files...'];
const streamMsg = document.getElementById('streamMsg');
const toolStatus = document.getElementById('toolStatus');
const toolResult = document.getElementById('toolResult');
const stateEl = document.getElementById('state');
const countEl = document.getElementById('count');
const input = document.getElementById('input');
let currentText = '';

window.ANIMATION_DURATION_MS = 12000;
window.ANIMATION_KEYFRAMES = [
  { time: 0, label: "idle" },
  { time: 2000, label: "user-input" },
  { time: 4000, label: "streaming" },
  { time: 6000, label: "tool-running" },
  { time: 8000, label: "tool-complete" },
  { time: 10000, label: "done" }
];
window.ANIMATION_VERIFICATION = [
  { label: "idle", state: "Idle" },
  { label: "user-input", inputValue: "find log files" },
  { label: "streaming", streamText: "I will use the glob tool" },
  { label: "tool-running", toolStatusText: "⏳ running" },
  { label: "tool-complete", toolStatusText: "✅ completed" },
  { label: "done", state: "Idle", msgCount: "5 msgs" }
];

function simulateStep() {
  switch(step) {
    case 0: // user types
      input.value = 'find log files';
      stateEl.textContent = '🤔 Thinking';
      stateEl.style.color = '#ffea00';
      break;
    case 1: // first tokens
      currentText = tokens[0];
      streamMsg.textContent = currentText;
      input.value = '';
      break;
    case 2: // more tokens
      currentText += tokens[1];
      streamMsg.textContent = currentText;
      break;
    case 3: // still streaming
      currentText += tokens[2];
      streamMsg.textContent = currentText;
      break;
    case 4: // tool starts
      currentText += tokens[3];
      streamMsg.textContent = currentText;
      toolStatus.textContent = '⏳ running';
      stateEl.textContent = '⚡ Executing';
      stateEl.style.color = '#448aff';
      toolResult.style.display = 'none';
      break;
    case 5: // tool completes
      toolStatus.textContent = '✅ completed';
      toolResult.style.display = 'block';
      stateEl.textContent = '🤔 Thinking';
      stateEl.style.color = '#ffea00';
      break;
    case 6: // assistant done
      streamMsg.textContent = 'Found 3 log files matching *.log.';
      stateEl.textContent = '💤 Idle';
      stateEl.style.color = '#ccc';
      countEl.textContent = '5 msgs';
      break;
  }
  step++;
  if (step > 6) step = 6;
}

function resetDemo() {
  step = 0;
  currentText = '';
  streamMsg.textContent = 'I will search for log files...';
  input.value = '';
  toolStatus.textContent = '⏳ running';
  toolResult.style.display = 'none';
  stateEl.textContent = '💤 Idle';
  stateEl.style.color = '#ccc';
  countEl.textContent = '3 msgs';
}

window.jumpToKeyframe = function(idx) {
  resetDemo();
  for (let i = 0; i <= idx; i++) { step = i; simulateStep(); }
};
window.resetAnimation = resetDemo;
window.getAnimationState = function() {
  return {
    state: stateEl.textContent,
    inputValue: input.value,
    streamText: streamMsg.textContent,
    toolStatusText: toolStatus.textContent,
    msgCount: countEl.textContent
  };
};
</script>
</body>
</html>
```

---

## 6. Testing Requirements

### Unit Tests

| Method | Test Case | Expected |
|--------|-----------|----------|
| `run` | Normal lifecycle | Returns 0, terminal restored |
| `shutdown` | During streaming | Loop exits, streaming cancelled |
| `resumeSession` | Valid UUID | Messages loaded, sessionId set |
| `resumeSession` | Invalid UUID | -1, no change |
| `currentSessionId` | After resume | Returns the resumed UUID |
| `currentSessionId` | Before any session | Empty string |
| `xHandleSubmit` | Normal text | Forwards to processGoal, state = Thinking |
| `xHandleSubmit` | `/sessions` | Triggers dialog |
| `xHandleSubmit` | `/quit` | Triggers shutdown |
| `xHandleSubmit` | Empty input | No-op |
| `xHandleInterrupt` | During streaming | Cancel called, state = Idle, system msg appended |
| `xHandleInterrupt` | While idle | No-op |
| `xOnToken` | Token received | Post to FTXUI, streamUpdate called |
| `xOnToolStart` | Tool begins | appendToolCall called, state = Executing |
| `xOnToolEnd` | Tool succeeds | updateToolCall called with Completed |
| `xOnToolEnd` | Tool fails | updateToolCall called with Failed |
| `xOnComplete` | Response done | endStream called, state = Idle, input re-enabled |
| `xOnError` | Error occurs | Error message appended, state = Error |
| `xOnInterrupted` | Aborted | Clean message appended |

### Integration Tests

| ID | Scenario | Steps | Expected |
|----|----------|-------|----------|
| INT‑TUI‑01 | Full session: input -> stream -> complete | Type goal, watch streaming, receive response | All messages rendered with correct roles/colors |
| INT‑TUI‑02 | Session resume | Quit TUI, relaunch with `--resume` | Previous messages visible, can continue |
| INT‑TUI‑03 | `/sessions` -> resume | Type `/sessions`, select a session | Session loads, messages displayed |
| INT‑TUI‑04 | Interrupt during tool | Ctrl+C while tool running | Tool cancelled, system message shown |
| INT‑TUI‑05 | `--no-permissions` | Launch with flag | All tool calls auto-approved |

---

## 7. CLI Entry Point

Wired in `main.cpp` as a CLI11 subcommand:

```cpp
auto tuiCmd = app.add_subcommand("tui", "Interactive terminal UI");
std::string resumeUuid;
bool noPermissions = false;
tuiCmd->add_option("--resume", resumeUuid, "Resume session by UUID");
tuiCmd->add_flag("--no-permissions", noPermissions, "Auto-approve tools");

// In dispatch:
if (*tuiCmd) {
    AgentStack stack = buildAgentStack(args);
    AgentTui tui(stack.core, stack.persistence, stack.skills,
                 [&]() { return b1Alive; }, noPermissions);
    if (!resumeUuid.empty())
        tui.resumeSession(resumeUuid);
    return tui.run();
}
```

### Additional Dependencies Linked

The `a0` binary must link:
- `ftxui::ftxui` (umbrella target)
- `md4c::md4c`
- `tui_lib` (the new static library containing all TUI sources)
