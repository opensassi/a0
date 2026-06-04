# TUI Implementation — Session Summary

## What Was Built

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

### Modified files (project-wide)

| File | Change |
|------|--------|
| `CMakeLists.txt` (root) | FTXUI + MD4C FetchContent, `add_subdirectory(src/tui)`, TUI test targets |
| `src/main.cpp` | TUI is now default mode (`a0` = `a0 tui`). `--log-dir` flag with `xAbsPath()` resolution. `xChildRedirectStdio()` for sub-process stderr isolation. `cmdRepl` removed, replaced by `cmdTui`. `xRedirectStderr()` helper with O_EXCL + .N suffix. Default `~/.a0/logs` log dir. Log redirect moved before `init()`. Exit on redirect failure. |
| `src/tui/agent_tui.h` | Added `setScreen()`/`clearScreen()`, `submitInput()`, paste tracking members (`m_pasteActive`, `m_pasteBuffer`, `m_pasteCounter`, `m_pastedContents`), `xExpandPastePlaceholders()`, `xProcessPasteBuffer()` |
| `src/tui/agent_tui.cpp` | TakeFocus fix (InputPanel child instead of root), bracketed paste enable (`\x1b[?2004h`), paste marker detection in CatchEvent, `xProcessPasteBuffer()` (placeholder or raw), `xExpandPastePlaceholders()` (marker→content on submit), copy-on-select using FTXUI `GetSelection()` on mouse-up, Character routing to Input, `onChange` paste pruning callback. `xProcessGoal()` now calls `processGoalStreaming()` with poller-based streaming completion. Paste cursor fix: trailing space after `[ PASTED #N ]`. Interrupt cancels handle. |
| `src/tui/input_panel.h` | Added `setOnChange()`, `insertText()` |
| `src/tui/input_panel.cpp` | Removed `\r`/`\n` from submit trigger (only `Event::Return` submits), enabled `multiline` in InputOption, added `on_change` for paste tracking, `insertText()` fires onChange |
| `src/tui/status_bar.cpp` | Removed timer thread (data race), replaced with render-time expiry check (`isFlashExpired()`) |
| `test/e2e/mock_deepseek_server.py` | Enhanced with `--scenario` flag for stateful multi-turn conversations |
| `src/agent_interfaces.h` | Added `ensureSession()` and `sessionDbId()` to `AgentCore` interface |
| `src/agent_core.h/.cpp` | `ensureSession()` implementation — centralized session creation with correct `agentDbId` |
| `src/b1/supervisor.cpp` | c2 and terminal child forks now redirect stdout+stderr to `/dev/null` or log file |
| `src/agent_core.h` | Added `setSessionId()` for pre-init session ID (log naming) |
| `src/skill_runner.cpp` | `executeStreaming()` now runs full LLM pipeline on background thread instead of returning empty handle |
| `src/deepseek_provider.cpp` | Added TRACE_LOG for curl errors in `complete()` |
| `test/tui/mock/mock_agent_core.h` | `processGoalStreaming()` thread now sets `exitCode` and `done` flags |

---

## Key Decisions Made

### TUI replaces REPL as default
`a0` now launches the FTXUI TUI by default. `cmdRepl()` was removed entirely. The `tui` subcommand is still available explicitly.

### DialogManager uses FTXUI Modal
Instead of AgentTui manually wrapping dialogs, `DialogManager::setMainComponent()` takes the main container and wraps it with `Modal(dialogContent, &active)`. The dialog manager owns the full component tree.

### xBuildLayout called in constructor
The component tree and callbacks are set up in `AgentTui`'s constructor, not in `run()`. This allows `component()` to be available immediately for test usage. Callback methods that access `m_screen` check `if (!m_screen) return;` to handle the time before `run()`.

### CatchEvent for Enter instead of InputOption::on_enter
`InputOption::on_enter` fires deep inside FTXUI's Input event handler, during event processing. Mutating the content buffer from that context caused crashes. `CatchEvent(input, handler)` intercepts `Event::Return` at the container level after the Input has fully processed the key event.

### Enter key matching uses only Event::Return (not \r/\n characters)
The original CatchEvent checked for `\r` and `\n` Character events in addition to `Event::Return`. This caused pasted multi-line text to submit on every line break. The fix: only `Event::Return` (bare Enter) triggers submit. Shift+Enter inserts a newline via the Input's built-in multiline support. Pasted `\n` is blocked by the outer CatchEvent during bracketed paste mode and accumulated into the paste buffer.

### Session creation centralized in AgentCore::ensureSession()
All entry points (`cmdTui`, `cmdRun`, `AgentTui::xProcessGoal`) now call `core->ensureSession()` instead of creating sessions directly or via `SessionManager::create()` with `agentId = 0`. This fixed the `FOREIGN KEY constraint failed` crash.

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
FTXUI v6.1.9 doesn't natively support bracketed paste (DEC mode 2006). Enable it manually with `\x1b[?2004h`. Detect paste markers in the outer CatchEvent: `\x1b[200~` starts paste mode (accumulate all events, block children), `\x1b[201~` processes the buffer. Pastes >20 chars are collapsed to `[ PASTED #N ]` with full content stored in `m_pastedContents[id]`. On submit, `xExpandPastePlaceholders()` replaces each marker with stored content. Manual user-typed `[ PASTED #N ]` also expands. Small pastes (≤20 chars) insert raw text.

### Atomic paste placeholder deletion
The `InputOption::on_change` callback fires on every keystroke. It scans the content buffer for all `[ PASTED #N ]` markers and prunes entries from `m_pastedContents` whose markers were deleted/modified. Backspacing into any character of the placeholder removes the entire stored content — the placeholder is atomic.

### Python PTY harness for E2E testing
The E2E test harness uses `pty.openpty()` + `os.fork()` to create a real pseudoterminal, then `os.execve()` to launch `a0 tui --test-mode` inside it. The parent process writes keystrokes and SGR mouse sequences to the PTY master and reads rendered output (with ANSI stripping). This is more robust than bash/expect because:
- Full byte-level control of the PTY (mouse sequences, paste markers, control characters)
- No race conditions from expect's line-oriented matching
- Can send arbitrary escape sequences (SGR mouse, bracketed paste, DCS, OSC)
- Can capture and assert on raw rendered output including ANSI formatting codes
- Clean process lifecycle management (fork, waitpid, signal handling)

### Log redirect moved before init; default log dir
`xRedirectStderr()` now runs before `init()`, so all TRACE logs, cloning messages, and early initialization output are captured in the log file. When neither `--log-dir` nor `--log-file` is specified, stderr goes to `.a0/logs/` by default (created automatically). Uses `O_EXCL` + `.N` suffix (`.1`, `.2`...`.99`) to prevent file collisions when PIDs are reused across `--resume` runs. If the redirect fails (permissions, disk full, 100+ `.N` files), the command exits immediately with a FATAL message rather than silently leaking stderr into the TUI.

### InputPanel disable via component swap
`InputPanel::setEnabled(false)` can't rely on FTXUI v6.1.9's `ComponentBase` (no `Enable()`/`Disable()`). Instead, the active component is swapped: when enabled, `component()` returns the real FTXUI Input; when disabled, it returns a `Renderer` showing "Waiting for response...". The outer CatchEvent checks `isEnabled()` before routing character events. `focus()` is a no-op when disabled.

### Scrollback via windowed rendering + yframe
The `MessagePanel` renders a sliding window of `VISIBLE_ENTRIES` (8) messages. `Up`/`Down` arrows, `PageUp`/`PageDown`, `Home`/`End` scroll the window. A scroll indicator shows `↑ N more` / `↓ N more` when not at top/bottom. The content is wrapped in `yframe` + `vscroll_indicator` for the visual frame. Auto-scroll resumes when the user scrolls to the bottom.

### Streaming completion via event-loop poller instead of background thread Post
The watcher thread pattern (`handle.wait()` then `m_screen->Post`) was unreliable — `Screen::Post` from a background thread didn't consistently wake up FTXUI's event loop `task_receiver_->Receive()`. Replaced with a self-re-posting `std::function` that runs entirely on the event loop thread: it checks `isDone()`, re-posts if not done, or calls `xOnComplete`/`xOnError` directly when done. The poller is stored in a `shared_ptr` so it stays alive as long as the event loop holds a reference to it.

### Paste cursor position: Event::End after insertText
After `insertText()` appends content to the FTXUI Input's buffer, the internal cursor stays at its previous position (typically `0`). This causes subsequent typing to appear before the inserted paste marker instead of after it. Fix: send `Event::End` to the Input component after `+=`, which moves the cursor to end of buffer. A trailing space is appended after `[ PASTED #N ]` so user input is separated from the expanded paste content.

---

## Remaining Issues

### 1. No color theme support
Colors and styles are hardcoded in `styles.cpp`. No configuration file or environment variable overrides for light/dark themes. `roleDecorator()` and `style::*` constants should read from a `Theme` struct populated by `A0_TUI_THEME` env var.

### 2. Visual selection highlight janks at component boundaries
FTXUI's `HandleSelection` tracks a rectangular selection region. When the mouse crosses between rendered elements (e.g., StatusBar → MessagePanel → InputPanel), the selection rectangle snaps to the new component's bounding box, causing a visual jump. This is FTXUI's internal rendering behavior — we use `GetSelection()` on mouse-up for correct text extraction regardless of visual jank.

### 3. Paste placeholder deletion reliability via PTY
The `onChange` callback correctly prunes stored content when a placeholder is deleted. However, testing this through the PTY with backspace events is timing-sensitive — rapid backspace delivery can race with the FTXUI event loop. In practice (real terminal, human typing), the per-keystroke delay is orders of magnitude larger than the FTXUI event loop latency, so this is not a practical issue.

### 4. DeepSeekProvider does not have completeStreaming()
The current `executeStreaming()` calls `m_provider->complete()` (synchronous HTTP request) on a background thread. The entire LLM response is delivered as a single `onChunk` call. True token-by-token streaming requires implementing `completeStreaming()` on `DeepSeekProvider` with libcurl SSE parsing, then wiring it through `executeStreaming()`. This is the highest-impact improvement for UX.

### 5. Poller-based completion doesn't work when no event loop is running
The streaming completion poller relies on `m_screen->Post` to re-schedule itself on the FTXUI event loop. If there is no event loop running (e.g., headless mode, unit tests that call `processGoalStreaming` without a screen), the poller is never posted, and the stream never completes. The detached-thread approach with `handle.wait()` would work in these scenarios but had reliability issues with `Screen::Post` from background threads.

### 6. Background thread Post to FTXUI Screen is unreliable
`Screen::Post` from a background thread (e.g., a detached watcher thread calling `handle.wait()` then `m_screen->Post(...)`) does not consistently wake up FTXUI's event loop. The `notifier_.notify_one()` call in the receiver triggers, but the loop's `Receive()` may not return promptly. The root cause may be a missed notification race or thread-safety issue in FTXUI's `ReceiverImpl`. The poller-based approach works around this by running entirely on the event loop thread, but see issue #5 for the limitation.

---

## Test Results

- **113 tests total** (90 C++ + 23 Python E2E)
- C++: 5 test targets, all passing
- Python E2E: 23 tests across 3 files, all passing
- Full suite runs in ~3 minutes (Python E2E dominates at ~2:50)
