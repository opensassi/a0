# TUI Implementation — Session Summary

## What Was Built (Previous Session)

### Source files in `src/tui/` (18 files)

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Build config linking FTXUI + MD4C |
| `styles.h/.cpp` | Color decorators, role labels (User/Assistant/Tool/System/Error), style constants |
| `session_manager.h/.cpp` | Session create/list/resume via PersistenceStore |
| `input_panel.h/.cpp` | FTXUI Input wrapper with Enter submit + Ctrl+C interrupt via CatchEvent |
| `status_bar.h/.cpp` | Top bar: session ID, agent state (Idle/Thinking/Executing/Error), b1 status, msg count |
| `message_panel.h/.cpp` | Scrollable message panel: append, streaming, tool blocks, history loading |
| `markdown_renderer.h/.cpp` | MD4C SAX-based markdown → FTXUI element tree (headings, code blocks, lists, etc.) |
| `dialog_manager.h/.cpp` | Stack-based modal overlays via FTXUI Modal (help, confirm, list) |
| `agent_tui.h/.cpp` | Facade: wires panels, mouse event routing, streaming callbacks, `/commands`, bracketed paste, copy-on-select |
| `clipboard.h/.cpp` | Clipboard copy via OSC 52 + xclip/wl-clipboard fallback |

### New files created in previous session (7)

| File | Purpose |
|------|---------|
| `src/mpsc.h` | Thread-safe MPSC channel with eventfd for poll() integration |
| `src/response_decoder.h/.cpp` | SSE/JSON response parser — feed bytes, emit structured events |
| `src/driven_provider.h/.cpp` | Async curl_multi-based LLM provider base class |
| `src/driven_core.h/.cpp` | State-machine tool-calling loop: Idle → AwaitingLlm → ExecutingTools |
| `src/app_core_thread.h/.cpp` | poll()-based event loop wrapping DrivenCore for future headless/separate-thread use |

### New files created in current session (3)

| File | Purpose |
|------|---------|
| `src/llm_provider.h` | Abstract async `LlmProvider` interface — replaces `InferenceProvider` |
| `src/deepseek_provider.h/.cpp` | `DeepSeekProvider : DrivenProvider` — DeepSeek-specific URL/auth/payload |
| `test/unit/test_driven_core_persistence.cpp` | 4 tests: user message persisted, assistant persisted, no session produces no persistence, session switch |

### Files modified in previous session

| File | Change |
|------|--------|
| `CMakeLists.txt` (root) | Added `response_decoder.cpp`, `driven_provider.cpp`, `driven_core.cpp`, `app_core_thread.cpp` to `LIB_SOURCES` |
| `src/tui/agent_tui.h` | Replaced `AgentCore* m_core`, `SkillManager* m_skills`, `StreamHandle`, streaming state with `DrivenProvider` + `DrivenCore`. Removed mpsc sender/receiver — DrivenCore is in-process, called synchronously via `xTickCore()`. |
| `src/tui/agent_tui.cpp` | `xHandleSubmit` now calls `m_drivenCore->submitGoal()` + `xTickCore()` instead of `m_core->processGoalStreaming()`. Renderer wrapper calls `xTickCore()` on each FTXUI frame. Interrupt calls `m_drivenCore->cancel()`. Session creation uses agentDbId for FK constraint. |
| `src/main.cpp` | Updated `cmdTui` to construct `AgentTui(apiKey, model, skillMgr, persistence, agentId, b1Status)` — no more `AppCoreThread`, sharedWakeup, or mpsc channels for TUI path. Mock URL passed to DrivenProvider via setMockUrl. |
| `test/unit/test_tui_integration.cpp` | Updated for new `AgentTui` constructor. Removed mpsc channel injection tests. |

### Files modified in current session

| File | Change |
|------|--------|
| `src/driven_provider.h/.cpp` | Refactored to implement `LlmProvider`. `xBuildPayload` + `xAddAuth` → `protected` pure virtual hooks. `m_apiKey/m_model/m_baseUrl` → `protected`. Constructor no longer sets DeepSeek-specific URL. |
| `src/deepseek_provider.h/.cpp` | **New** `DeepSeekProvider : DrivenProvider`. Overrides `xBuildPayload` (OpenAI-compatible JSON) + `xAddAuth` (Bearer token). Reads `DEEPSEEK_API_KEY` env var. Sets `m_baseUrl` to DeepSeek endpoint. |
| `src/driven_core.h/.cpp` | Constructor: `DrivenProvider*` → `LlmProvider*`. Added `runSync()` + `m_lastResult`. No `tools_for_prompt` call in `xBuildInitialMessages`. `xStartLlmRequest` uses `buildBasePrompt()` from disk instead of broken `getPrompt("system-base")`. |
| `src/main.cpp` | `xRegisterSystemHandlers` no longer takes `InferenceProvider*`. `AgentStack` slimmed (removed `DefaultContextManager`, `DefaultDependencyResolver`, `DefaultSkillRunner*`, `DefaultAgentCore*`; added `DeepSeekProvider llmProvider`). `cmdRun` uses `DrivenCore::runSync()`. `runSkill` → stub error. `cmdTui` injects `&stack.llmProvider` + `sessionDbId`/`sid` into `AgentTui`. |
| `src/tui/agent_tui.h/.cpp` | Constructor takes `LlmProvider*` instead of `apiKey+model`. Added `sessionDbId`/`sessionUuid` params. `m_provider` changed from `unique_ptr<DrivenProvider>` to raw `LlmProvider*` pointer. Constructor calls `m_drivenCore->setSession()` if sessionDbId > 0. `resumeSession()` now calls `m_drivenCore->setSession(dbId, uuid)`. |
| `src/system_handlers.h` | Removed `xToolsForPrompt` declaration. Removed `InferenceProvider` forward declaration. |
| `src/app_core_thread.h/.cpp` | Uses `DeepSeekProvider` subclass (new) instead of `DrivenProvider`. |
| `CMakeLists.txt` | Removed `agent_core.cpp`, `skill_runner.cpp`, old `deepseek_provider.cpp` from `LIB_SOURCES`. Added new `deepseek_provider.cpp`. Added `test_driven_core_persistence` target. Removed old test targets referencing deprecated code. |
| `test/unit/test_tui_integration.cpp` | Added `NullLlmProvider` mock. Added `InjectedSessionIsUsedOnFirstSubmit` + `ResumeSessionWiresDrivenCore` tests. Updated all `AgentTui` constructor calls to new signature `(provider, skillMgr, persistence, ...)`. |
| `src/llm_provider.spec.md` | **New** — spec for abstract async `LlmProvider` interface |
| `src/driven_provider.spec.md` | Rewritten — implements `LlmProvider`, protected pure virtual hooks |
| `src/deepseek_provider.spec.md` | Rewritten (was old `InferenceProvider` implementation; now `DrivenProvider` subclass) |
| `src/driven_core.spec.md` | Updated for `LlmProvider*`, `runSync()`, no `tools_for_prompt` |
| `src/main.spec.md` | Rewritten — slim `AgentStack`, unified `DrivenCore` path |
| `src/system_handlers.spec.md` | Rewritten — removed `xToolsForPrompt`, no `InferenceProvider` |
| `src/app_core_thread.spec.md` | Updated — uses `DeepSeekProvider` subclass internally |
| `src/agent_core.spec.md` | Added **DEPRECATED** header |
| `src/skill_runner.spec.md` | Added **DEPRECATED** header |
| `technical-specification.md` | Updated §2 (interface hierarchy), §5 (test tables), §8 (file layout) |
| `src/tui/technical-specification.md` | Updated for `LlmProvider*` injection + session params |

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

### DrivenCore integrated into FTXUI thread (single-thread approach)
DrivenCore runs inside the TUI's FTXUI event loop, NOT a separate thread. `xTickCore()` is called from a Renderer wrapper on every FTXUI frame. When the core is busy, `RequestAnimationFrame()` is called to keep the FTXUI render loop active. This is a simplification over the planned two-thread architecture — the AppCoreThread exists as infrastructure but the current TUI path owns DrivenCore directly.

### curl_multi used for async HTTP
DrivenProvider uses `curl_multi` for non-blocking HTTP. `startRequestStreaming()` sets up curl handles; `tick()` drives `curl_multi_perform()` and collects events. The write callback stores raw response data, which is fed to `ResponseDecoder` for SSE/JSON parsing.

### CURLOPT_POSTFIELDS stores a dangling pointer if body is local
`curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str())` stores a pointer, not a copy. If `body` is a local string that goes out of scope, curl reads garbage. Fixed by storing the body in the `EasyHandle` struct before setting curl options.

### First LLM request omits tool schemas
The initial LLM request in `DrivenCore::submitGoal` starts with `includeTools=false` (matching the old streaming path behavior). Tool schemas are only included in follow-up requests after tool execution. This avoids the mock server returning `tool_calls` instead of a content response. **Current issue:** this also prevents the real DeepSeek API from making tool calls — the LLM sees tool names in the base prompt text but can't use function calling.

### curl_multi_perform needs curl_multi_wait before it to drive async DNS
`DrivenProvider::tick()` calls `curl_multi_perform()` but curl's internal async DNS resolver has its own sockets that must be polled for DNS responses. Without `curl_multi_wait(m_multi, nullptr, 0, 0, nullptr)` before each `perform`, DNS resolution never completes and curl returns `CURLE_COULDNT_RESOLVE_HOST`. The zero-timeout `wait` is a single `poll()` syscall (~1µs) that drives curl's internal I/O non-blockingly. This affected both mock server and real DeepSeek API usage.

### Non-streaming responses need explicit handling in xOnComplete
`DrivenProvider` sends `"stream": true` in the request body, but the mock server returns plain JSON (not SSE). The `ResponseDecoder::xFlushBuffer` method detects JSON mode and emits a single `Complete` event with no preceding `LlmToken`. `AgentTui::xOnComplete` only handled the streaming path (`m_streamingEntryIndex >= 0`), so the response was silently dropped. Fixed by adding an `else` branch that appends the response as a `MessageEntry` directly.

### BufferedSocket replaces one-byte-at-a-time IPC recv
`recvMessage()` in `ipc_protocol.cpp` read one byte per `poll()` + `recv()` syscall — ~300 syscalls for a 150-byte REGISTER message. Replaced with `BufferedSocket`, a persistent per-connection buffered reader that reads up to 100 bytes per call and accumulates in a per-fd buffer. All callers updated: b1 supervisor (socket map per agent), c2 listener (map replaces vector), a0 terminal mode. `RecvResult` enum replaces magic integers: `RECV_OK` (0), `RECV_AGAIN` (1), `RECV_ERR` (-1). Callers previously closed connections on any non-zero return; they now distinguish `RECV_AGAIN` (retry) from `RECV_ERR` (close).

### Concurrency model specification written
Created `specs/concurrency-model.md` (840 lines, 10 sections) covering all 9 concurrency contexts across 3 processes (a0, b1, c2). Includes C4 container diagrams, mutex domain analysis, 4 sequence diagrams, and 4 identified issues. Reviewed by 7-expert panel producing 8 revisions.

### LlmProvider abstract interface
Replaces synchronous `InferenceProvider` with async tick-based contract. `DrivenCore` depends on `LlmProvider*` only, not on any concrete implementation.

### DrivenProvider as universal base class
The `curl_multi` machinery is universal. Provider-specific differences (URL format, auth header, payload schema) are abstracted via `xBuildPayload()` and `xAddAuth()` pure virtual hooks. Subclasses override these; all transport logic is inherited.

### DeepSeekProvider as DrivenProvider subclass
Minimal implementation — only config (DeepSeek URL, Bearer auth, OpenAI-compatible JSON format, `DEEPSEEK_API_KEY` env var resolution). All HTTP/curl/event-loop logic lives in the base class. Future providers follow the same pattern.

### Unified code path (cmdRun + cmdTui)
Both entry points use `DrivenCore` with `LlmProvider*`. `cmdRun` uses `runSync()` (synchronous poll loop). `cmdTui` uses `tick()` from the FTXUI render loop. No more divergent `AgentCore` vs `DrivenCore` paths.

### Base prompt from disk (buildBasePrompt)
`DrivenCore::xStartLlmRequest` reads `prompts/base.md` via `a0::buildBasePrompt(m_skillMgr)`. The old path tried `m_skillMgr->getPrompt("system-base")` which failed because `skills/system/base/skill.json` was never created — the only copy of the base prompt lived in `prompts/base.md` (loaded by `AgentCore`/`SkillRunner` via a separate code path).

### tools_for_prompt removed
No separate LLM analysis call for intent/tool selection. The base prompt lists all available tools and instructs the LLM to call them directly. `xBuildInitialMessages` now just creates the user message — follow-up requests with tool schemas let the LLM make function calls.

### Session injection into AgentTui
`sessionDbId` + `sessionUuid` passed to `AgentTui` constructor to prevent duplicate session creation (main.cpp created one session, AgentTui's `xHandleSubmit` created a second). Constructor calls `m_drivenCore->setSession()` — previously missing, causing all goal-related messages to be persisted to session id 0 (lost).

### resumeSession now wires DrivenCore
`AgentTui::resumeSession()` was missing the `m_drivenCore->setSession(dbId, uuid)` call. New messages submitted after resume went to the wrong session or were lost. Fixed by adding the call after setting `m_sessionDbId`/`m_sessionUuid`.

### xOnComplete streaming path overwrites accumulated text
The SSE final chunk's `finish_reason:"stop"` event produces `Complete{""}` (empty text). `xOnComplete` calls `m_messagePanel->streamUpdate(index, fullOutput)` where `fullOutput==""`, overwriting the accumulated `m_streamingText` with empty string. The response vanishes from the TUI display while persisting correctly in the DB (DrivenCore's `xFinishGoal` uses `m_accumText` which runs before `xOnComplete`). **Fix:** use `m_streamingText` instead of `fullOutput` in the streaming branch.

---

## Remaining Issues

### 1. c2 signal handler — async-signal-safety (RESOLVED)
Section 7.4 of the concurrency spec flagged `dashboard.shutdown()` and `listener.shutdown()` as potentially non-async-signal-safe. Investigation confirmed neither shutdown function acquires any mutex: `DashboardServer::shutdown()` sets two booleans and closes a socket; `C2Listener::shutdown()` sets one boolean and closes a socket. Both are POSIX async-signal-safe. The concurrency spec should be updated to document this resolution.

### 2. hex_session_id uses std::mt19937 (not CSPRNG)
Section 4.2 of the concurrency spec notes that `hex_session_id` seeds `std::mt19937` from `std::random_device`. Sufficient for session UUIDs used as identifiers, but not suitable for security tokens. Should be documented with a note in the concurrency model spec.

### 3. close(stdinPipe1) without lock in stream reader thread
Spec §6.1 documents that `command_runner.cpp:256` closes `stdinPipe1` without holding `state->mutex`. If `sendInput()` is called concurrently, the write may go to a recycled fd. Mitigated in practice by the race window (close happens after pipe EOF, sendInput called before child exits). Fix: wrap in `lock_guard`.

### 4. DeepSeekProvider cross-thread access from skill executor (RESOLVED)
`skill_runner.cpp:348` called `m_provider->complete()` from a background thread. `skill_runner.cpp` is no longer compiled. The new path uses `DrivenProvider`/`DrivenCore` single-threaded in the FTXUI event loop.

### 5. g_timeoutFired read without std::atomic_signal_fence
Spec §9.3 documents that `g_timeoutFired` (declared `volatile sig_atomic_t`) is read at `command_runner.cpp:348` without a matching volatile qualification. On some architectures the compiler may hoist the read before `alarm()`. Fix: replace with `std::atomic<int>` + `std::atomic_signal_fence`.

### 6. xOnComplete streaming branch overwrites accumulated text
`agent_tui.cpp` — `xOnComplete` passes `fullOutput` (from SSE final chunk's `Complete{""}`) to `streamUpdate()`, overwriting the accumulated response text with empty string. The response vanishes from the TUI display while persisting correctly in the DB. **Fix:** use `m_streamingText` instead of `fullOutput` in the streaming branch.

### 7. includeTools=false prevents LLM tool calling
`driven_core.cpp:90` — first LLM request omits tool schemas (`xStartLlmRequest(false)`). The LLM sees tool names in the base prompt text but cannot make API function calls without the `tools` parameter in the request. Combined with mock server legacy path returning `tool_calls` fixture when `tools` is present. **Fix:** change to `true` and update mock server.

### 8. Session seq interleaving between init and goal phases
SkillManager recording (from SessionContext git worktree init) and DrivenCore use independent `m_seq` counters targeting the same session, producing interleaved seq values in session export. **Fix:** use separate `subSessionId` for init-phase vs goal-phase messages.

---

## Architecture Overview (Current State)

```
a0 binary (main.cpp)
├─ cmdRun:
│   └─ DrivenCore::runSync() → DeepSeekProvider → LLM
│
├─ cmdTui (default):
│   └─ AgentTui (FTXUI event loop thread)
│       ├─ DrivenCore (state machine: Idle → AwaitingLlm → ExecutingTools)
│       │   └─ LlmProvider* (injected: DeepSeekProvider)
│       │       └─ ResponseDecoder (feed bytes → events)
│       ├─ MessagePanel, InputPanel, StatusBar, DialogManager
│       └─ SessionManager (PersistenceStore CRUD)

Shared hierarchy: LlmProvider → DrivenProvider → DeepSeekProvider
Both paths use DrivenCore with same LlmProvider interface.
```

**Event flow**:
1. User types + Enter → `setOnSubmit` → `xHandleSubmit`
2. `xHandleSubmit` → appends user message, calls `m_drivenCore->submitGoal(goal)`
3. `submitGoal` → `xBuildInitialMessages` (no tools_for_prompt — base prompt from disk) → `xBuildToolSchemas` → `xStartLlmRequest(false)` (no tool schemas on first request)
4. FTXUI frame render → `coreTicker` Renderer → `xTickCore()` → `m_drivenCore->tick()` → `m_provider->tick()` → `curl_multi_perform`
5. Events (LlmToken, ToolStart, ToolEnd, Complete, Error) handled by `xHandleEvent`
6. If core still busy after tick, `RequestAnimationFrame()` keeps the render loop going

**File count**: 18 src/tui/ files + 9 new src/ files = 27 total

---

## Test Results

| Suite | Status | Failures |
|-------|--------|----------|
| C++ unit tests (33 targets) | 33/33 PASS | — |
| Python E2E (39 tests) | 39/39 PASS | — |
