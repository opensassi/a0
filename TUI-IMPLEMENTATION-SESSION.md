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
| `agent_tui.h/.cpp` | Facade: wires panels, mouse event routing, streaming callbacks, `/commands` |
| `clipboard.h/.cpp` | Clipboard copy via OSC 52 + xclip/wl-clipboard fallback |

### Test files (6 files, 52 tests)

| File | Tests |
|------|-------|
| `test/unit/test_tui_styles.cpp` | 10 — roleDecorator/roleLabel for all roles |
| `test/unit/test_tui_session_manager.cpp` | 10 — CRUD, resume, edge cases |
| `test/unit/test_tui_panels.cpp` | 26 — MessagePanel, InputPanel, StatusBar, DialogManager |
| `test/unit/test_tui_markdown.cpp` | 16 — headings, lists, code blocks, streaming mode |
| `test/unit/test_tui_integration.cpp` | 13 — AgentTui construction, session resume, layout rendering |
| `test/tui/mock/test_screen.cpp` | TestScreen harness (FixedSize + background loop + event injection) |
| `test/tui/mock/mock_persistence_store.h` | In-memory PersistenceStore |
| `test/tui/mock/mock_agent_core.h` | MockAgentCore with Scenario interface for canned streaming |

### Test targets in CMakeLists.txt

- `test_tui_styles`, `test_tui_session_manager`, `test_tui_panels`, `test_tui_markdown`, `test_tui_integration`
- `tui_test_mock` library (test_screen.cpp)

### E2E infrastructure

- `test/e2e/test_tui_e2e.sh` — bash script using `expect` to drive `a0 tui --test-mode --mock-api`
- `test/e2e/run_all_tests.sh` — Phase 4: TUI E2E added

### Modified files (project-wide)

| File | Change |
|------|--------|
| `CMakeLists.txt` (root) | FTXUI + MD4C FetchContent, `add_subdirectory(src/tui)`, TUI test targets |
| `src/main.cpp` | TUI is now default mode (`a0` = `a0 tui`). `--log-dir` flag with `xAbsPath()` resolution. `xChildRedirectStdio()` for sub-process stderr isolation. `cmdRepl` removed, replaced by `cmdTui`. |
| `src/agent_interfaces.h` | Added `ensureSession()` and `sessionDbId()` to `AgentCore` interface |
| `src/agent_core.h/.cpp` | `ensureSession()` implementation — centralized session creation with correct `agentDbId` |
| `src/b1/supervisor.cpp` | c2 and terminal child forks now redirect stdout+stderr to `/dev/null` or log file |
| `test/e2e/run_all_tests.sh` | Phase 3 → Phase 4 renumbering, TUI E2E section |

## Key Decisions Made

### TUI replaces REPL as default
`a0` now launches the FTXUI TUI by default. `cmdRepl()` was removed entirely. The `tui` subcommand is still available explicitly.

### DialogManager uses FTXUI Modal
Instead of AgentTui manually wrapping dialogs, `DialogManager::setMainComponent()` takes the main container and wraps it with `Modal(dialogContent, &active)`. The dialog manager owns the full component tree.

### xBuildLayout called in constructor
The component tree and callbacks are set up in `AgentTui`'s constructor, not in `run()`. This allows `component()` to be available immediately for test usage. Callback methods that access `m_screen` check `if (!m_screen) return;` to handle the time before `run()`.

### CatchEvent for Enter instead of InputOption::on_enter
`InputOption::on_enter` fires deep inside FTXUI's Input event handler, during event processing. Mutating the content buffer from that context caused crashes. `CatchEvent(input, handler)` intercepts `Event::Return` at the container level after the Input has fully processed the key event.

### Enter key matching includes \r and \n
Terminals send `\r` (CR, 0x0D) on Enter, but FTXUI's `Event::Return` is `\n` (LF, 0x0A). The CatchEvent checks both.

### Session creation centralized in AgentCore::ensureSession()
All entry points (`cmdTui`, `cmdRun`, `AgentTui::xProcessGoal`) now call `core->ensureSession()` instead of creating sessions directly or via `SessionManager::create()` with `agentId = 0`. This fixed the `FOREIGN KEY constraint failed` crash.

### Children redirected to /dev/null by default
When neither `--log-file` nor `--log-dir` is specified, all forked children (b1, c2, terminal) redirect stdout+stderr to `/dev/null` via `xChildRedirectStdio("")`. This prevents garbage text from sub-processes corrupting the TUI display.

### Sub-process logs via --log-dir
When `--log-dir <path>` is specified, log files are created as `<dir>/a0-<sessionId>-<pid>.log` (a0 stderr), `<dir>/a0-<sessionId>-<pid>-b1.log` (b1), `<dir>/a0-<sessionId>-<pid>-c2.log` (c2), `<dir>/a0-<sessionId>-<pid>-term.log` (terminal). The `A0_LOG_DIR` and `A0_SESSION_ID` env vars are exported for supervisor children.

### copy-to-clipboard approach (NOT WORKING)
A `clipboard.h/.cpp` module was added supporting OSC 52 + xclip + wl-clipboard. The TUI tracks mouse-down→drag→release events and reads PRIMARY selection via `xclip -o -selection primary` on mouse-up after a drag. **This does not work in practice** because:
- Terminals set PRIMARY selection during native text selection, but in FTXUI's alternate screen mode the selection mechanism is terminal-specific
- `xclip -o -selection primary` may read from the wrong X11 window
- The approach needs rethinking — likely needs to track selected text positions within FTXUI's own element tree and extract the text programmatically

## Remaining Issues

### 1. Copy-on-select doesn't work
The current approach of reading `xclip -o -selection primary` on mouse-up is unreliable in FTXUI's alternate screen. Need to implement proper text selection:
- Track mouse start/end positions within the MessagePanel's scroll area
- Map screen coordinates to message indices and character offsets
- Extract the text from the `MessageEntry` vector
- Apply visual selection highlighting (inverted background) to selected elements
- Copy on mouse-up via the clipboard module

### 2. InputPanel::setEnabled is a no-op
FTXUI v6.1.9 `ComponentBase` doesn't have Enable()/Disable(). The `setEnabled(false)` in `xProcessGoal` just sets a bool but doesn't actually prevent the Input from receiving keyboard events. Need to find a way to disable the Input component (e.g., swap between a focused Renderer placeholder and the actual Input).

### 3. TestScreen::start() uses ScreenInteractive::FixedSize()
`FixedSize()` returns a `ScreenInteractive` by value which is not movable (has `std::atomic<bool>` member). The current workaround stores it in a local variable and passes a pointer. This is fragile — if the screen goes out of scope while the loop is running, undefined behavior. Need to either use a heap allocation or restructure.

### 4. Interactive integration tests are DISABLED
Tests that need `AgentTui::run()` (full event loop) are marked `DISABLED_` because `run()` blocks on `Loop::Run()`. Proper testing requires running `run()` on a background thread while the test thread sends events and asserts. The `TestScreen` class was designed for this but the integration isn't wired yet.

### 5. SessionManager::create() still uses agentId=0
`SessionManager::create(uuid)` calls `createSession(uuid, 0, 0, 0)` which causes FK constraint failures in SQLite. This method is no longer called from the main TUI code path (xProcessGoal now uses `ensureSession()`), but it's still present for the `/sessions` command. Should accept `agentId` parameter or use `PersistenceStore`'s `findSessionByUuid()`.

### 6. No scrollback in MessagePanel
The MessagePanel uses a simple `Container::Vertical` with a `Renderer`. There's no scrollbar or scrollback mechanism. Long conversations will overflow the visible area with no way to scroll up. FTXUI's `ScrollBase` component should be used to wrap the message content.

### 7. No color theme support
Colors and styles are hardcoded in `styles.cpp`. No configuration file or environment variable overrides for light/dark themes.

### 8. b1/c2 log files missing during initial runs
The `A0_LOG_DIR` and `A0_SESSION_ID` env vars must be set before the b1 fork. If a previous b1 instance is running (from before the `--log-dir` feature was added), `kill-all` must be run first to clear the stale process. Fixed by documenting `a0 kill-all` usage.

## Test Results

- **39/39 tests pass** (including all 52 TUI assertions)
- `test_supervisor` timeout fixed — `shutdown()` now properly kills the forked c2 child process
- Full suite runs in ~10 seconds
