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

### New files created in this session (7)

| File | Purpose |
|------|---------|
| `src/mpsc.h` | Thread-safe MPSC channel with eventfd for poll() integration |
| `src/response_decoder.h/.cpp` | SSE/JSON response parser — feed bytes, emit structured events |
| `src/driven_provider.h/.cpp` | Async curl_multi-based LLM provider, replaces streaming in DeepSeekProvider |
| `src/driven_core.h/.cpp` | State-machine tool-calling loop: Idle → AwaitingLlm → ExecutingTools |
| `src/app_core_thread.h/.cpp` | poll()-based event loop wrapping DrivenCore for future headless/separate-thread use |

### C++ Test files (75 assertions in 5 files)

| File | Tests |
|------|-------|
| `test/unit/test_tui_styles.cpp` | 10 — roleDecorator/roleLabel for all roles |
| `test/unit/test_tui_session_manager.cpp` | 10 — CRUD, resume, edge cases |
| `test/unit/test_tui_panels.cpp` | 41 — MessagePanel, InputPanel, StatusBar, DialogManager, scrollback, Input disable |
| `test/unit/test_tui_markdown.cpp` | 16 — headings, lists, code blocks, streaming mode |
| `test/unit/test_tui_integration.cpp` | 13 — AgentTui construction, session resume, layout rendering, interactive submitInput |

### E2E test infrastructure and Python tests (36 tests)

| Directory/file | Tests | Purpose |
|----------------|-------|---------|
| `test/agent_e2e/conftest.py` | — | TuiDriver (PTY-based), MockServer, AgentSubprocess, strip_ansi |
| `test/agent_e2e/test_crashes.py` | 10 | Binary existence, CLI flags, consecutive goals, stress test |
| `test/agent_e2e/test_tui_rendering.py` | 12 | TUI PTY-driven: submit goal, /help, /sessions, /clear, interrupt, paste-routing, mouse drag, scrollback |
| `test/agent_e2e/test_scenarios.py` | 5 | Scenario-driven agent tests with multi-turn mock server |
| `test/agent_e2e/test_clipboard.py` | 5 | Copy-on-select: status bar drag, goal text selection, visual highlight, crash |
| `test/agent_e2e/test_paste.py` | 6 | Bracketed paste: collapse large >20 chars, raw small, multiple placeholders, manual reference, newline block, cursor after paste |
| `test/agent_e2e/mock_wrappers/` | — | xclip/wl-copy mock scripts for clipboard testing |
| `test/agent_e2e/scenarios/` | — | JSON scenario files (simple_tool_call, multi_turn_workflow, tool_error) |
| `test/agent_e2e/run.sh` | — | Orchestrator script |

### Files modified in this session

| File | Change |
|------|--------|
| `CMakeLists.txt` (root) | Added `response_decoder.cpp`, `driven_provider.cpp`, `driven_core.cpp`, `app_core_thread.cpp` to `LIB_SOURCES` |
| `src/tui/agent_tui.h` | Replaced `AgentCore* m_core`, `SkillManager* m_skills`, `StreamHandle`, streaming state with `DrivenProvider` + `DrivenCore`. Removed mpsc sender/receiver — DrivenCore is in-process, called synchronously via `xTickCore()`. |
| `src/tui/agent_tui.cpp` | `xHandleSubmit` now calls `m_drivenCore->submitGoal()` + `xTickCore()` instead of `m_core->processGoalStreaming()`. Renderer wrapper calls `xTickCore()` on each FTXUI frame. Interrupt calls `m_drivenCore->cancel()`. Session creation uses agentDbId for FK constraint. |
| `src/main.cpp` | Updated `cmdTui` to construct `AgentTui(apiKey, model, skillMgr, persistence, agentId, b1Status)` — no more `AppCoreThread`, sharedWakeup, or mpsc channels for TUI path. Mock URL passed to DrivenProvider via setMockUrl (in-progress). |
| `test/unit/test_tui_integration.cpp` | Updated for new `AgentTui` constructor (apiKey, model, skillMgr, persistence, agentId, b1Status). Removed mpsc channel injection tests. |

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
The initial LLM request in `DrivenCore::submitGoal` starts with `includeTools=false` (matching the old streaming path behavior). Tool schemas are only included in follow-up requests after tool execution. This avoids the mock server returning `tool_calls` instead of a content response.

---

## Remaining Issues (All Caused by Current Changes)

### 1. curl_multi transfer never completes — LLM response not rendered
**Symptom**: test_tui_submit_goal_shows_response fails with timeout (15s). The mock server receives the tools_for_prompt request (via old DeepSeekProvider) but NOT the DrivenProvider's streaming request. The `curl_multi_perform` call from `DrivenProvider::tick()` does not establish the HTTP connection to the mock server.

**Trace evidence**: TRACE log shows `DrivenCore::submitGoal`, `DrivenProvider::startRequestStreaming`, and `DrivenProvider::tick` called, but the second `[MOCK] POST` never appears. The `curl_multi_perform` returns `running=0` immediately without initiating the connection.

**Root cause investigation**: The `curl_multi` handle is initialized in `DrivenProvider` constructor (`curl_multi_init()`). The easy handle is created and configured in `startRequestStreaming`, then added to `m_multi` via `curl_multi_add_handle`. On the next `tick()`, `curl_multi_perform(m_multi, &running)` is called. `running` should be 1 (transfer in progress). But the connection is never established to the mock server — no TCP SYN packet reaches the mock port.

**Hypothesis**: curl may be trying HTTPS (TLS) even though the URL is HTTP. The URL set via `setMockUrl` is `http://127.0.0.1:PORT/v1/chat/completions` and the SSL verification skip check looks for "localhost" or "127.0.0.1" in `m_baseUrl`. This should work for plain HTTP.

**To fix**: Verify the URL is correctly set on the curl handle. Test with `curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L)` to see connection diagnostics. Or bypass curl_multi entirely and use a synchronous curl_easy_perform in a thread (like the old code did).

### 2. Interrupt doesn't render "Interrupted" message
**Symptom**: test_tui_interrupt_streaming fails. After sending Ctrl+C during streaming, the TUI output shows `Thinking` instead of `Idle` or the "Interrupted" system message.

**Root cause**: `xHandleInterrupt` correctly calls `m_drivenCore->cancel()`, appends an "Interrupted" `MessageEntry`, and sets `m_agentState = AgentState::Idle`. But the FTXUI event loop doesn't re-render after these UI updates — the captured screen output still shows the pre-interrupt state.

**To fix**: Call `m_screen->RequestAnimationFrame()` at the end of `xHandleInterrupt` to trigger a render cycle. Or ensure the setOnSubmit callback chain triggers a render via FTXUI's Post mechanism after the interrupt callback returns.

### 3. Mock URL not propagated to DrivenProvider
**Symptom**: When `--mock-api` is specified on the command line, it's passed to the old `DeepSeekProvider` via `stack.provider.setMockUrl(mockUrl)` in `AgentStack`. But the new `DrivenProvider` inside `AgentTui` doesn't receive this URL.

**Current behavior**: `DrivenProvider` connects to the real DeepSeek API (`https://api.deepseek.com/v1/chat/completions`) which requires a valid API key. Tests using mock servers fail because the provider ignores the mock URL.

**To fix**: Add a `setMockUrl(const std::string&)` method to `AgentTui` (which forwards to its internal `DrivenProvider`). Call it from `cmdTui` in `main.cpp` when `mockUrl` is not empty:
```cpp
if (!mockUrl.empty()) {
    tui.setMockUrl(mockUrl);
}
```

### 4. (Minor) Interrupt test expects old status text
The interrupt test checks for `"Interrupted"` in the captured output. The current handler appends a message with `MessageRole::System` and content `"Interrupted"`. The message panel renders this with the role label `┌─ System` above the content. The test's `capture()` strips ANSI but should find the `Interrupted` text once #2 is fixed.

---

## Architecture Overview (Current State)

```
AgentTui (FTXUI event loop thread)
├─ DrivenProvider (curl_multi, async HTTP)
├─ DrivenCore (state machine: Idle → AwaitingLlm → ExecutingTools)
│   └─ ResponseDecoder (feed bytes → events)
├─ MessagePanel, InputPanel, StatusBar, DialogManager
└─ SessionManager (PersistenceStore CRUD)
```

**Event flow**:
1. User types + Enter → `setOnSubmit` → `xHandleSubmit`
2. `xHandleSubmit` → appends user message, calls `m_drivenCore->submitGoal(goal)`
3. `submitGoal` → `xBuildInitialMessages` (tools_for_prompt via old DeepSeekProvider), `xStartLlmRequest(false)` (DrivenProvider without tools)
4. FTXUI frame render → `coreTicker` Renderer → `xTickCore()` → `m_drivenCore->tick()` → `m_provider->tick()` → `curl_multi_perform`
5. Events (LlmToken, ToolStart, ToolEnd, Complete, Error) handled by `xHandleEvent`
6. If core still busy after tick, `RequestAnimationFrame()` keeps the render loop going

**File count**: 18 src/tui/ files + 7 new src/ files = 25 total

---

## Test Results

| Suite | Status | Failures |
|-------|--------|----------|
| C++ unit tests (39 targets) | 39/39 PASS | — |
| Python E2E (36 tests) | 25/28 PASS | test_tui_submit_goal_shows_response (curl_multi never connects), test_tui_interrupt_streaming (render not triggered), test_tui_osc_52_sequence (clipboard env) |

All failures are caused by the current implementation changes. The three failures must all be fixed — none are pre-existing.

---

## Immediate Fixes Needed

### Fix 1: Mock URL propagation
Add `setMockUrl()` to `AgentTui` → `DrivenProvider`. Simplest change, affects all mock-based tests.

### Fix 2: curl_multi transfer not completing
The foundamental issue: `curl_multi` doesn't connect to the mock server. Possible causes:
- URL not propagated (Fix 1 might fix this)
- Multi handle initialized but curl_multi_socket_action needed instead of curl_multi_perform
- SSL/TLS attempted on HTTP URL
- Easy handle options lost when added to multi

**Fallback**: If curl_multi can't be made to work, replace DrivenProvider with a simple synchronous curl_easy_perform on a background thread, matching the old DeepSeekProvider pattern.

### Fix 3: Interrupt render trigger
Add `RequestAnimationFrame()` in `xHandleInterrupt` after UI state updates.

### Fix 4: Heartbeat mechanism
If `RequestAnimationFrame()` from xTickCore doesn't keep FTXUI rendering, add a `Screen::Post([]{});` after `xHandleSubmit` to guarantee at least one render cycle after goal submission.
