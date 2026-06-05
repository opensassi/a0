# TUI Implementation — Session Summary

## What Was Built (Previous Sessions)

### Source files in `src/tui/` (18 original files)

| File                       | Purpose                                                                                |
| -------------------------- | -------------------------------------------------------------------------------------- |
| `CMakeLists.txt`           | Build config linking FTXUI + MD4C (NO a0_lib/persistence_lib deps)                     |
| `styles.h/.cpp`            | Color decorators, role labels (User/Assistant/Tool/System/Error), style constants      |
| `input_panel.h/.cpp`       | FTXUI Input wrapper with Enter submit + Ctrl+C interrupt via CatchEvent                |
| `status_bar.h/.cpp`        | Top bar: session ID, agent state (Idle/Thinking/Executing/Error), b1 status, msg count |
| `message_panel.h/.cpp`     | Scrollable message panel: append, streaming, tool blocks, history loading              |
| `markdown_renderer.h/.cpp` | MD4C SAX-based markdown → FTXUI element tree (headings, code blocks, lists, etc.)      |
| `dialog_manager.h/.cpp`    | Stack-based modal overlays via FTXUI Modal (help, confirm, list)                       |
| `agent_tui.h/.cpp`         | Facade: MPSC sender/receiver only, no core references, event dispatch                  |
| `clipboard.h/.cpp`         | Clipboard copy via OSC 52 + xclip/wl-clipboard fallback                                |

### Core infrastructure files (7)

| File                           | Purpose                                                                  |
| ------------------------------ | ------------------------------------------------------------------------ |
| `src/mpsc.h`                   | Thread-safe MPSC channel with eventfd for poll() integration             |
| `src/response_decoder.h/.cpp`  | SSE/JSON response parser — feed bytes, emit structured events            |
| `src/driven_provider.h/.cpp`   | Async curl_multi-based LLM provider base class                           |
| `src/driven_core.h/.cpp`       | State-machine tool-calling loop: Idle → AwaitingLlm → ExecutingTools     |
| `src/app_core_thread.h/.cpp`   | ppoll()-based event loop wrapping DrivenCore for background thread use   |
| `src/llm_provider.h`           | Abstract async `LlmProvider` interface — replaces `InferenceProvider`    |
| `src/deepseek_provider.h/.cpp` | `DeepSeekProvider : DrivenProvider` — DeepSeek-specific URL/auth/payload |

### Files deleted

| File                                     | Reason                                                                   |
| ---------------------------------------- | ------------------------------------------------------------------------ |
| `src/tui/session_manager.h/.cpp`         | Session ops moved to MPSC protocol (ListSessions/ResumeSession commands) |
| `test/unit/test_tui_session_manager.cpp` | Deleted with SessionManager                                              |

### Test files created (this session)

| File                         | Purpose                                                    |
| ---------------------------- | ---------------------------------------------------------- |
| `test/e2e/conftest.py`       | Python PTY harness: TuiDriver, AgentSubprocess, MockServer |
| `test/e2e/test_tui_e2e.py`   | 12 TUI E2E tests (PTY-based, replaces bash/expect)         |
| `test/e2e/test_agent_e2e.py` | 7 headless agent E2E tests (uses `a0 run` subcommand)      |

### Files modified (this session)

| File                                     | Change                                                                                                                                                                                                                                                                                                     |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/mpsc.h`                             | Added `SetSession`, `ListSessions`, `ResumeSession` to Command; `SessionReady`, `SessionList`, `SessionHistory`, `SessionMessage` to AppCoreEvent                                                                                                                                                          |
| `src/app_core_thread.h/.cpp`             | Handles `SetSession` (core.setSession + SessionReady), `ListSessions` (loadSessions + SessionList), `ResumeSession` (findSessionByUuid + loadMessages + SessionHistory). Minimum ppoll timeout 10ms to prevent busy-spin.                                                                                  |
| `src/tui/agent_tui.h/.cpp`               | Complete rewrite — no LlmProvider*, no DrivenCore, no PersistenceStore*, no SessionManager. Only `Sender<Command>` + `Receiver<AppCoreEvent>`. `drainEvents()` replaces `xTickCore()`. `xHandleSubmit` → `cmdSender.send(SubmitGoal{})`. Added `RequestAnimationFrame()` after drain to trigger re-render. |
| `src/tui/message_panel.h/.cpp`           | `loadHistory` uses `mpsc::SessionMessage` instead of `persistence::Message`                                                                                                                                                                                                                                |
| `src/tui/CMakeLists.txt`                 | Removed `session_manager.cpp`. Removed `a0_lib` and `persistence_lib` link deps.                                                                                                                                                                                                                           |
| `src/main.cpp`                           | Added `#include app_core_thread.h`. Both `cmdTui` and `cmdRun` create AppCoreThread + MPSC channels. AgentTui receives only MPSC handles.                                                                                                                                                                  |
| `src/persistence/persistence_store.h`    | Added `SessionRow` struct and `loadSessions(limit)` virtual method                                                                                                                                                                                                                                         |
| `src/persistence/sqlite_store.h/.cpp`    | Implemented `loadSessions()` with SQL subquery                                                                                                                                                                                                                                                             |
| `src/response_decoder.cpp`               | `xProcessJsonChunk`: emit `ToolStart` events from `finish_reason: "stop"` handler before early return (fixes non-streaming tool_calls responses)                                                                                                                                                           |
| `src/driven_core.cpp`                    | Changed `xStartLlmRequest(false)` → `true` so tool schemas are sent on first request                                                                                                                                                                                                                       |
| `src/command_runner.cpp`                 | `g_timeoutFired`: `volatile sig_atomic_t` → `std::atomic<int>` + `std::atomic_signal_fence`. `close(stdinPipe1)` wrapped in `lock_guard<mutex>`, sets `stdinFd = -1`.                                                                                                                                      |
| `src/session_context.cpp`                | All `executeToolWithMeta` calls pass `subSessionId=-1` for init-phase recording                                                                                                                                                                                                                            |
| `src/hex_session_id.h`                   | Replaced `std::mt19937` with `/dev/urandom` read into `std::array<uint32_t, 4>`                                                                                                                                                                                                                            |
| `CMakeLists.txt`                         | Added `tui_lib` to `ENABLE_TRACE` target list; removed deprecated test targets                                                                                                                                                                                                                             |
| `src/tui/technical-specification.md`     | Complete rewrite v3.0 — thin-client TUI architecture, emphatic boundary enforcement                                                                                                                                                                                                                        |
| `AGENTS.md`                              | Added 8-rule debugging protocol; removed MCP Tools section                                                                                                                                                                                                                                                 |
| `test/e2e/run_e2e_tests.sh`              | Replaced piped-a0 tests with pytest runner                                                                                                                                                                                                                                                                 |
| `test/e2e/test_tui_e2e.sh`               | Replaced bash/expect test with pytest runner                                                                                                                                                                                                                                                               |
| `test/e2e/run_all_tests.sh`              | Updated Phase 4 to use Python pytest                                                                                                                                                                                                                                                                       |
| `test/unit/test_tui_integration.cpp`     | Rewritten for new AgentTui constructor + MPSC event injection                                                                                                                                                                                                                                              |
| `test/tui/mock/mock_persistence_store.h` | Added `loadSessions` override                                                                                                                                                                                                                                                                              |

**Removed from build (kept on disk as reference):**
`src/agent_core.h/.cpp`, `src/skill_runner.h/.cpp`, `src/deepseek_provider.h/.cpp` (old `InferenceProvider` implementation)

---

## Key Decisions Made

### TUI replaces REPL as default

`a0` now launches the FTXUI TUI by default. `cmdRepl()` was removed entirely. The `tui` subcommand is still available explicitly.

### DialogManager uses FTXUI Modal

Instead of AgentTui manually wrapping dialogs, `DialogManager::setMainComponent()` takes the main container and wraps it with `Modal(dialogContent, &active)`. The dialog manager owns the full component tree.

### xBuildLayout called in constructor

The component tree and callbacks are set up in `AgentTui`'s constructor, not in `run()`. This allows `component()` to be available immediately for test usage.

### CatchEvent for Enter instead of InputOption::on_enter

`InputOption::on_enter` fires deep inside FTXUI's Input event handler, during event processing. Mutating the content buffer from that context caused crashes. `CatchEvent(input, handler)` intercepts `Event::Return` at the container level after the Input has fully processed the key event.

### Enter key matching uses only Event::Return (not \r/\n characters)

The original CatchEvent checked for `\r` and `\n` Character events in addition to `Event::Return`. This caused pasted multi-line text to submit on every line break. The fix: only `Event::Return` (bare Enter) triggers submit. Shift+Enter inserts a newline via the Input's built-in multiline support. Pasted `\n` is blocked by the outer CatchEvent during bracketed paste mode and accumulated into the paste buffer.

### Session creation uses agentDbId

`xHandleSubmit` creates sessions via `m_sessionMgr->create(uuid, m_agentId)` where `m_agentId` comes from `AgentStack.core->agentDbId()`. This ensures the FOREIGN KEY constraint on the `sessions` table is satisfied (agent row must exist). Passing 0 caused the `FOREIGN KEY constraint failed` crash.

### Children redirected to /dev/null by default

When neither `--log-file` nor `--log-dir` is specified, all forked children (b1, c2, terminal) redirect stdout+stderr to `/dev/null` via `xChildRedirectStdio("")`. This prevents garbage text from sub-processes corrupting the TUI display.

### Sub-process logs via --log-dir

When `--log-dir <path>` is specified, log files are created as `<dir>/a0-<sessionId>-<pid>.log` (a0 stderr), `<dir>/a0-<sessionId>-<pid>-b1.log` (b1), `<dir>/a0-<sessionId>-<pid>-c2.log` (c2), `<dir>/a0-<sessionId>-<pid>-term.log` (terminal). The `A0_LOG_DIR` and `A0_SESSION_ID` env vars are exported for supervisor children.

### TakeFocus must be called on a leaf component, not the root

FTXUI's `ComponentBase::TakeFocus()` walks UP the tree from `this` to root, calling `SetActiveChild` at each level. When called on the root CatchEvent (which has `parent_ == nullptr`), the loop body never executes — no `SetActiveChild` is called anywhere, so the Container::Vertical's internal `selected_` stays at 0 (StatusBar Renderer). All keyboard events are silently consumed by the un-focusable Renderer. **Call `m_inputPanel->component()->TakeFocus()` instead** to walk up from the Input, setting each container's active child correctly.

### Copy-on-select uses FTXUI GetSelection(), not xclip PRIMARY

The original approach called `xclip -o -selection primary` on mouse-up to read X11 PRIMARY selection. This fails in FTXUI's alternate screen because terminal-level text selection doesn't set PRIMARY when mouse events are captured by FTXUI. The fix: track mouse-down/move/release in the outer CatchEvent and call `m_screen->GetSelection()` (FTXUI's own API that maps drag coordinates to rendered text) on mouse-up. The clipboard module emits OSC 52 and falls back to xclip/wl-copy.

### No SelectionChange callback

FTXUI's `ScreenInteractive::SelectionChange()` fires on every selection change including mid-drag. Using it would call `copyToClipboard()` (which spawns xclip/popen and writes OSC 52 to stdout) on every mouse-move during a drag, causing performance issues and racing with FTXUI's render writes to stdout. Instead, track drag state in the CatchEvent's mouse handler and call `GetSelection()` only on mouse-up.

### Bracketed paste with numbered placeholders

FTXUI v6.1.9 doesn't natively support bracketed paste (DEC mode 2006). Enable it manually with `\x1b[?2004h`. Detect paste markers in the outer CatchEvent: `\x1b[200~` starts paste mode (accumulate all events, block children), `\x1b[201~` processes the buffer. Pastes >20 chars are collapsed to `[ PASTED #N ]` with full content stored in `m_pastedContents[id]`. On submit, `xExpandPastePlaceholders()` replaces each marker with stored content. Small pastes (≤20 chars) insert raw text.

### Atomic paste placeholder deletion

The `InputOption::on_change` callback fires on every keystroke. It scans the content buffer for all `[ PASTED #N ]` markers and prunes entries from `m_pastedContents` whose markers were deleted/modified.

### Python PTY harness for E2E testing

The E2E test harness uses `pty.openpty()` + `os.fork()` to create a real pseudoterminal, then `os.execve()` to launch `a0 tui --test-mode` inside it. The parent process writes keystrokes and SGR mouse sequences to the PTY master and reads rendered output (with ANSI stripping).

### Log redirect moved before init; default log dir

`xRedirectStderr()` now runs before `init()`, so all TRACE logs, cloning messages, and early initialization output are captured in the log file. When neither `--log-dir` nor `--log-file` is specified, stderr goes to `.a0/logs/` by default.

### InputPanel disable via component swap

`InputPanel::setEnabled(false)` can't rely on FTXUI v6.1.9's `ComponentBase` (no `Enable()`/`Disable()`). Instead, the active component is swapped: when enabled, `component()` returns the real FTXUI Input; when disabled, it returns a `Renderer` showing "Waiting for response...".

### Scrollback via windowed rendering + yframe

The `MessagePanel` renders a sliding window of `VISIBLE_ENTRIES` (8) messages.

### AppCoreThread wired as active (C2 architecture, this session)

The TUI no longer owns DrivenCore directly. `AppCoreThread` runs on a background thread owning `DeepSeekProvider` + `DrivenCore`. All communication goes through MPSC channels (`Command` variants from TUI to core, `AppCoreEvent` variants from core to TUI). The TUI is a thin rendering client with zero core references. This was a deliberate architectural shift from the previous single-thread approach (C1) to the two-thread approach (C2), making the TUI just another client — consistent with the headless architecture where b1/c2 drive the same core.

### TUI is a thin rendering client (no core references, this session)

AgentTui holds zero core references:

- NO `DrivenCore` or `DrivenProvider`
- NO `LlmProvider` pointer
- NO `SkillManager` pointer or call
- NO `PersistenceStore` reference
- NO `SessionManager` (deleted)
- NO tool execution, NO LLM interaction, NO session management

All core functionality (LLM calls, tool execution, session persistence, skill management) lives in AppCoreThread on the background thread. The TUI only renders FTXUI elements and forwards user keystrokes/mouse events via MPSC. This boundary is enforced by architecture — the TUI `CMakeLists.txt` does not link `a0_lib` or `persistence_lib`.

### SessionManager deleted (this session)

All session operations (list, resume) go through MPSC commands/events:

- `/sessions` → `cmdSender.send(ListSessions{20})` → core loads from SQLite → `SessionList` event back to TUI
- Session resume → `cmdSender.send(ResumeSession{uuid})` → core loads messages → `SessionHistory{dbId, uuid, messages}` event back to TUI
- Session creation happens in main.cpp during bootstrap (before thread start), not in the TUI

### drainEvents needs RequestAnimationFrame (this session)

After processing MPSC events in the main loop, the TUI must call `m_screen->RequestAnimationFrame()` to request a re-render. Without this, message panel updates are silently dropped. The old `xTickCore()` called this when the core was busy, but `drainEvents()` didn't — events were processed but never displayed. This was the root cause of the TUI showing "Thinking" forever even though AppCoreThread completed both LLM requests and sent all events back via MPSC.

### E2E tests migrated from bash/expect to Python pytest (this session)

The old `test/e2e/run_e2e_tests.sh` piped input via `echo "goal" | timeout N a0` which broke when `a0` defaulted to TUI mode (piped stdin is ignored by FTXUI). Replaced with Python `TuiDriver` (PTY-based, uses `pty.openpty() + os.fork() + os.execve()`) and `AgentSubprocess` (uses `a0 run` subcommand correctly). Python provides better test structure (pytest fixtures, assertions), supports mouse events and bracketed paste, and requires zero external dependencies (uses Python `pty` module from stdlib). The Python `conftest.py` with `TuiDriver` and `MockServer` replaces both the bash/expect TUI test AND the piped-stdin agent tests.

### conftest.py shared between TUI and agent E2E tests (this session)

The `test/e2e/conftest.py` provides:

- `MockServer` — context manager for the mock DeepSeek API server
- `TuiDriver` — PTY-based driver for `a0 tui --test-mode` with keystroke injection and output capture
- `AgentSubprocess` — headless runner using `a0 run <goal>` with stdout/stderr capture and assertions

### tool_calls in non-streaming responses need explicit decoder handling (this session)

The `ResponseDecoder::xProcessJsonChunk` returned early on `finish_reason: "stop"` without parsing `message.tool_calls` in the JSON response. For non-streaming responses (like the mock server returning plain JSON instead of SSE), the tool_calls were silently dropped. Fixed by emitting `ToolStart` events from within the `finish_reason` handler before emitting the `Complete` event.

### curl_multi_wait with timeout=0 is needed for curl I/O progress

`DrivenProvider::tick()` calls `curl_multi_wait(m_multi, nullptr, 0, 0, nullptr)` before each `curl_multi_perform()`. This single `poll()` syscall drives curl's internal async I/O including DNS resolution. Without it, `curl_multi_perform()` is stuck at `running=1` with empty `responseBody` — curl's internal socket buffers are never drained. The zero-timeout wait is NOT a no-op: curl internally processes socket state during the wait call even with timeout=0.

### includeTools=true on first request (this session)

Changed `xStartLlmRequest(false)` → `true` in `DrivenCore::submitGoal()` so the first LLM request includes tool schemas. Without this, the real DeepSeek API cannot make function calls on the first turn — the LLM sees tool names in the base prompt text but cannot use structured function calling. The mock server's `_legacy_respond` returns `tool_calls` fixture when `tools` is present.

### ENABLE_TRACE must be explicitly added to each CMake target (this session)

The `CMakeLists.txt` `option(ENABLE_TRACE)` guards `target_compile_definitions(... PRIVATE TRACE)` but only lists `a0_lib`, `b1_lib`, and `c2_lib`. `tui_lib` was missing, causing all `TRACE_LOG` calls in `src/tui/` to be silently compiled out. Debugging showed `AppCoreThread` events being sent but `AgentTui::drainEvents` never traced — the events were reaching the TUI but the re-render bug was obscured because the TRACE output was missing. Fixed by adding `tui_lib` to the ENABLE_TRACE target list.

### curl_multi used for async HTTP

DrivenProvider uses `curl_multi` for non-blocking HTTP. `startRequestStreaming()` sets up curl handles; `tick()` drives `curl_multi_perform()` and collects events. The write callback stores raw response data, which is fed to `ResponseDecoder` for SSE/JSON parsing.

### CURLOPT_POSTFIELDS stores a dangling pointer if body is local

`curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str())` stores a pointer, not a copy. If `body` is a local string that goes out of scope, curl reads garbage. Fixed by storing the body in the `EasyHandle` struct before setting curl options.

### Unified code path (cmdRun + cmdTui, this session)

Both entry points use `AppCoreThread` with `LlmProvider*`. `cmdRun` creates `AppCoreThread`, sends `SetSession` + `SubmitGoal`, polls `evtReceiver` for `Complete`/`Error`, prints result, shuts down. `cmdTui` creates `AppCoreThread`, sends `SetSession`, runs `AgentTui::run()` (FTXUI loop draining MPSC events from background thread). No more divergent `AgentCore` vs `DrivenCore` paths. Both paths share the same `AppCoreThread` infrastructure.

### Base prompt from disk (buildBasePrompt)

`DrivenCore::xStartLlmRequest` reads `prompts/base.md` via `a0::buildBasePrompt(m_skillMgr)`. The old path tried `m_skillMgr->getPrompt("system-base")` which failed because `skills/system/base/skill.json` was never created — the only copy of the base prompt lived in `prompts/base.md` (loaded by `AgentCore`/`SkillRunner` via a separate code path).

### tools_for_prompt removed

No separate LLM analysis call for intent/tool selection. The base prompt lists all available tools and instructs the LLM to call them directly. `xBuildInitialMessages` now just creates the user message — follow-up requests with tool schemas let the LLM make function calls.

### Session injection into AgentTui

`sessionDbId` + `sessionUuid` passed to `AgentTui` constructor to prevent duplicate session creation (main.cpp created one session, AgentTui's `xHandleSubmit` created a second). Constructor calls `m_drivenCore->setSession()` — previously missing, causing all goal-related messages to be persisted to session id 0 (lost).

### resumeSession now goes through MPSC (this session)

`AgentTui::resumeSession()` was removed. Session resumption sends `ResumeSession{uuid}` via cmdSender. The AppCoreThread processes it, loads history from persistence, sends back `SessionHistory{dbId, uuid, messages}` event, and calls `core.setSession()`. The TUI receives the event and loads history into the message panel.

### xOnComplete streaming path uses m_streamingText, not fullOutput

The SSE final chunk's `finish_reason:"stop"` event produces `Complete{""}` (empty text). `xOnComplete` calls `m_messagePanel->streamUpdate(index, fullOutput)` where `fullOutput==""`, overwriting the accumulated `m_streamingText` with empty string. The response vanishes from the TUI display while persisting correctly in the DB (DrivenCore's `xFinishGoal` uses `m_accumText` which runs before `xOnComplete`). **Fix:** use `m_streamingText` instead of `fullOutput` in the streaming branch.

---

## Remaining Issues

---

## Architecture Overview (Current State)

```
a0 binary (main.cpp)
├─ cmdRun:
│   └─ AppCoreThread (background thread)
│       ├─ DeepSeekProvider → DrivenCore
│       └─ ppoll loop: ticks DrivenCore, sends events via MPSC
│
├─ cmdTui (default):
│   ├─ MAIN THREAD: AgentTui (FTXUI render client)
│   │   ├─ MessagePanel, InputPanel, StatusBar, DialogManager
│   │   ├─ m_cmdSender (MPSC Sender<Command>)  ——→  SubmitGoal, Cancel, SetSession, ListSessions, ResumeSession
│   │   └─ m_evtReceiver (MPSC Receiver<AppCoreEvent>)  ←——  LlmToken, ToolStart/End, Complete, Error, SessionReady/List/History
│   └─ BACKGROUND THREAD: AppCoreThread
│       ├─ DeepSeekProvider + DrivenCore
│       └─ ppoll loop: drains commands, ticks core, sends events, calls wakeupFn

Communication: MPSC channels (thread-safe, eventfd-based, no shared state)
TUI has ZERO core references: no DrivenCore, no LlmProvider, no SkillManager, no PersistenceStore
```

**Event flow**:

1. User types + Enter → `setOnSubmit` → `xHandleSubmit`
2. `xHandleSubmit` → appends user message, sends `cmdSender.send(SubmitGoal{goal})`
3. FTXUI frame render → `drainEvents()` (no tick — core is on background thread)
4. AppCoreThread drains `SubmitGoal`, calls `core.submitGoal()`, starts curl
5. AppCoreThread ticks core → curl completes → events via `evtSender`
6. AppCoreThread calls `wakeupFn()` → `screen->Post(Task{})`
7. FTXUI event loop wakes → `RunOnce()` processes posted task, renders
8. `drainEvents()` picks up MPSC events → `xHandleCoreEvent()`
9. `RequestAnimationFrame()` forces re-render if events were processed

**File count**: 8 src/tui/ files + 7 core src/ files = 15 total (SessionManager deleted)

---

## Test Results

| Suite                              | Status     | Failures |
| ---------------------------------- | ---------- | -------- |
| C++ unit tests (32 targets)        | 32/32 PASS | —        |
| Agent E2E (7 tests, Python pytest) | 7/7 PASS   | —        |
| TUI E2E (12 tests, Python PTY)     | 12/12 PASS | —        |
