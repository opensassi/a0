# TUI Component Taxonomy

## 1. Component Tree

```
app                          CatchEvent (paste/mouse/scroll tracking)
└── bg-panel                 Renderer (DialogManager wrapper)
    └── dialog-overlay       Modal
        ├── main-scroll       Container::Vertical
        │   ├── status-bar    Renderer (StatusBar)
        │   ├── msg-panel     Renderer (yframe + vscroll_indicator)
        │   └── input-bar     CatchEvent (Enter/Ctrl+C)
        │       ├── [enabled]  Input (multiline)         ← InputPanel::realInput
        │       └── [disabled] Renderer("Waiting...")    ← InputPanel::disabledInput
        └── dialog-stack      Renderer (DialogManager overlay)
```

## 2. Component Taxonomy

### `app` — AgentTui facade

| Field | Value |
|-------|-------|
| **Class** | `a0::tui::AgentTui` in `src/tui/agent_tui.h/.cpp` |
| **Parent** | root |
| **Children** | `bg-panel` |
| **Purpose** | FTXUI event loop orchestrator. Owns all sub-panels, the MPSC sender/receiver pair, and the screen. Builds the layout in constructor via `xBuildLayout()`. |

**State held:**
- `m_cmdSender` (MPSC `Sender<Command>`) — user → core
- `m_evtReceiver` (MPSC `Receiver<AppCoreEvent>`) — core → user
- `m_sessionUuid`, `m_sessionDbId`
- `m_agentState` — current `AgentState` enum
- `m_streamingText` — accumulator for in-flight assistant response
- `m_assistantEntryIndex` — index of the single `┌─ Assistant` entry for the current user goal
- `m_pasteActive`, `m_pasteBuffer`, `m_pastedContents` — bracketed paste state
- `m_mouseDown`, `m_mouseMoved` — copy-on-select drag tracking

**Events consumed (via `xHandleCoreEvent`):**
| Event | Handler | Effect |
|-------|---------|--------|
| `LlmToken` | `xOnToken` | `msg-panel` → appendOrUpdateAssistantText (accumulates into current text child) |
| `ToolStart` | `xOnToolStart` | `msg-panel` → appendAssistantTool; `status-bar` → setAgentState(Executing); `input-bar` → disable |
| `ToolEnd` | `xOnToolEnd` | `msg-panel` → updateLastAssistantTool; `status-bar` → setAgentState(Thinking) |
| `RoundComplete` | `xOnRoundComplete` | `msg-panel` → endCurrentAssistantText (seals text child, next round starts fresh) |
| `Complete` | `xOnComplete` | `msg-panel` → appendOrUpdateAssistantText + finalizeAssistant; `status-bar` → setIdle; `input-bar` → enable+focus |
| `Error` | `xOnError` | `msg-panel` → appendOrUpdateAssistantText + finalizeAssistant; `status-bar` → setIdle; `input-bar` → enable+focus |
| `SessionReady` | `xOnSessionReady` | `status-bar` → setSessionId |
| `SessionList` | `xOnSessionList` | `dialog-stack` → show session picker |
| `SessionHistory` | `xOnSessionHistory` | `msg-panel` → clear+loadHistory; `status-bar` → setId+count |

**Events emitted (via `m_cmdSender.send`):**
| Trigger | Command |
|---------|---------|
| user types + Enter | `SubmitGoal{text}` |
| Ctrl+C | `Cancel{}` |
| Ctrl+Q / `:q` / `/quit` | `Shutdown{}` |
| `/sessions` | `ListSessions{20}` |
| dialog selects session | `ResumeSession{uuid}` |

**Mutation pattern:** `drainEvents()` is called each FTXUI frame inside `run()`. It dequeues all pending `AppCoreEvent` variants and dispatches them via `xHandleCoreEvent`. After processing, calls `m_screen->RequestAnimationFrame()` if events were handled.

---

### `status-bar` — Top status line

| Field | Value |
|-------|-------|
| **Class** | `a0::tui::StatusBar` in `src/tui/status_bar.h/.cpp` |
| **Parent** | `main-scroll` (child 0, fixed height 1) |
| **FTXUI type** | `Renderer` |
| **Purpose** | Displays agent state, session ID (8-char), b1 connection, message count, and transient flash messages. |

**Public API:**
```
setSessionId(uuid)       → shows first 8 chars
setAgentState(Idle|Thinking|Executing|Error)  → colored label
setB1Connected(bool)     → ✓ or --
setMessageCount(n)       → "N msgs"
showStatus(msg, secs)    → transient flash with auto-expire
```

**State held:** `m_sessionId`, `m_agentState`, `m_b1Connected`, `m_messageCount`, `m_flashMessage`, `m_flashTimer`.

**Interaction effects:** Every state change in `app`'s event handlers calls one or more `status-bar` mutation methods. This component is purely reactive — it never emits events.

---

### `msg-panel` — Message history scroll

| Field | Value |
|-------|-------|
| **Class** | `a0::tui::MessagePanel` in `src/tui/message_panel.h/.cpp` |
| **Parent** | `main-scroll` (child 1, flex-grow) |
| **FTXUI type** | `CatchEvent(Renderer(yframe + vscroll_indicator))` — click-to-toggle handled via `reflect`+`ToolHit` box tracking |
| **Purpose** | Scrollable display of all messages: user queries, assistant responses (streamed or complete), tool call blocks with status/output, system messages. |

**Per-assistant children model:**
- One `┌─ Assistant` entry per user goal, created by `beginAssistant()` in `xHandleSubmit`
- Assistant entries have a `children` vector (`std::vector<MessageEntry>`)
- Text segments are appended as children with `role=Assistant`
- Tool calls are appended as children with `role=Tool`
- Multiple LLM rounds within one user goal accumulate into the same assistant entry
- `RoundComplete` seals the current text child; new tokens create a new child
- `Complete` finalizes the entire assistant entry

**Rendering of an assistant entry:**
```
vbox
├── text("┌─ Assistant") | green            ← header (one per goal)
├── paragraph("I'll look up the files...")  ← child[0]: text
├── xRenderToolBlock(child)                 ← child[1]: tool (collapsible)
├── paragraph("Here are the results...")    ← child[2]: text
└── text("")
```

**Scroll model:**
- ALL entries are rendered in a single vbox element tree (no virtual window)
- `yframe` clips the vbox to the container height
- `ftxui::focus` decorator on the target entry tells `yframe` where to scroll
- Scrolling is controlled by `focusIndex` (target entry index) and `autoScroll` bool:
  - `autoScroll = true` → focus is always on the last entry (new content auto-follows)
  - `autoScroll = false` (manual scroll) → focus stays on `focusIndex`
- Scroll events (PgUp/PgDn/Home/End, mouse wheel) are handled by `app`'s outer `CatchEvent` and forwarded to `msg-panel` methods

**Click-to-toggle:**
- The msg-panel `CatchEvent` uses `ftxui::reflect` boxes to detect clicks on tool entries
- Top-level tool entries (from history) use `entryBoxes` — toggled directly
- Tool children inside assistant entries use `toolHits` — `ToolHit{entryIdx, childIdx, box}` stored in `Impl` with `box` storage in the vector element (stable across frames)
- Left mouse click (release, no drag) toggles `collapsed` on the matching tool entry
- Returns `false` (propagate) so `app`'s outer handler still sees mouse events for copy-on-select

**Public API (used by `app` event handlers):**
```
append(entry)               → adds user/assistant/system/tool message (top-level, for history)
clear()                     → wipe all messages
beginAssistant()            → creates a single Assistant entry with empty children, returns index
appendOrUpdateAssistantText(asstIdx, text)  → appends/updates streaming text child
endCurrentAssistantText(asstIdx)            → seals the current streaming text child
appendAssistantTool(asstIdx, name, state, args)  → appends tool child to assistant
updateLastAssistantTool(asstIdx, state, output)  → updates last Running tool child
finalizeAssistant(asstIdx)                 → seals text + sets assistant to non-streaming
loadHistory(msgs)           → bulk load from session restore (flat, no children)
scrollToBottom()            → re-enables autoScroll, focus follows last entry
count()                     → number of entries
scrollUp/Down(n)            → manual scroll (disables autoScroll, adjusts focusIndex)
scrollToTop()               → jump to top
isAtBottom()                → autoScroll decision
```

**Rendering details:**
- All entries rendered in a single vbox element tree
- `yframe` clips to container; `vscroll_indicator` shows scrollbar
- Focused entry receives `| ftxui::focus` decorator each frame:
  - `autoScroll = true` → last entry gets focus
  - `autoScroll = false` → entry at `focusIndex` gets focus
- Tool entries (children): single-line `🔧 name args  ✅/⏳/❌ status` (collapsed by default), output shown when toggled open. Click-to-toggle via `reflect`+`ToolHit` box tracking.
- Streaming assistant child: plain `paragraph(content)` when non-empty, `⏳ thinking` when empty. No blinking cursor.
- Markdown assistant messages rendered via `md-renderer`
- Scroll via `PageUp`/`PageDown` (handled in `app`'s outer `CatchEvent`, forwarded to `msg-panel`)

**Interaction effects:**
| Incoming event → | msg-panel call |
|-----------------|----------------|
| User submits | `append(User, text)` then `beginAssistant()` |
| LlmToken | `appendOrUpdateAssistantText(asstIdx, m_streamingText)` |
| ToolStart | `appendAssistantTool(asstIdx, name, Running, args)` |
| ToolEnd | `updateLastAssistantTool(asstIdx, Completed/Failed, output)` |
| RoundComplete | `endCurrentAssistantText(asstIdx)` |
| Complete | `appendOrUpdateAssistantText` + `finalizeAssistant(asstIdx)` |
| Error | `appendOrUpdateAssistantText` + `finalizeAssistant(asstIdx)` + `append(Error, msg)` |
| SessionHistory | `clear()` then flat `append()` per message |
| /clear command | `clear()` |

---

### `input-bar` — Bottom input field

| Field | Value |
|-------|-------|
| **Class** | `a0::tui::InputPanel` in `src/tui/input_panel.h/.cpp` |
| **Parent** | `main-scroll` (child 2, fixed min-height 3) |
| **FTXUI type** | dynamic: `CatchEvent(Input.multiline) \| Renderer("Waiting...")` |
| **Purpose** | Text input with Enter-submit, Ctrl+C-interrupt, Up/Down history, and paste support. |

**Public API:**
```
setOnSubmit(cb)       → Enter key handler
setOnInterrupt(cb)    → Ctrl+C handler
setOnChange(cb)       → keystroke handler (paste placeholder cleanup)
insertText(text)      → programmatic insert (paste expansion)
setEnabled(bool)      → swap between real Input and disabled placeholder
setPlaceholder(text)  → prompt text when empty
clear()               → clear content buffer
focus()               → request FTXUI input focus
addHistory(prompt)    → save to local history ring
loadHistory(path)     → load from file
saveHistory(path)     → save to file
```

**Key behavior:**
- `InputOption::multiline = true` for multi-line input with Shift+Enter
- `CatchEvent` intercepts `Event::Return` (bare Enter → submit, Shift+Enter → newline)
- `CatchEvent` intercepts char 3 (Ctrl+C → interrupt)
- `Bracketed paste` detected in `app`'s outer CatchEvent, accumulated into `m_pasteBuffer`
- Pastes > 20 chars collapsed to `[PASTED #N]` placeholder, expanded on submit via `xExpandPastePlaceholders()`

**Disabled state:** When `setEnabled(false)`, `component()` returns a `Renderer` showing dim "Waiting for response..." instead of the real Input. Switching back calls `TakeFocus()` on `input-bar`'s real Input.

---

### `dialog-stack` — Modal overlay

| Field | Value |
|-------|-------|
| **Class** | `a0::tui::DialogManager` in `src/tui/dialog_manager.h/.cpp` |
| **Parent** | `dialog-overlay` (modal layer of FTXUI `Modal`) |
| **FTXUI type** | `Renderer` (top-of-stack dialog component) |
| **Purpose** | Stack-based modal dialog system. Shows help, confirmations, and session selection lists. |

**Public API:**
```
setMainComponent(component)  → wraps main layout inside Modal
show(dialog, title, onDismiss)  → push dialog onto stack
dismiss()                    → pop top dialog
dismissAll()                 → clear stack
isActive()                   → whether any dialog is shown
showHelp()                   → pre-built keybinding reference
showConfirm(title, msg, onConfirm)  → yes/no dialog
showList(title, items, onSelect)    → selectable list (sessions)
```

**Dialogs provided:**
| Method | Renders | User action |
|--------|---------|-------------|
| `showHelp()` | Keybinding table (Enter/Ctrl+C/Up/Down/:/Tab/Escape) | Escape to dismiss |
| `showConfirm()` | Title + message + [Yes] [No] | onConfirm(bool) |
| `showList()` | Scrollable items list | onSelect(index) |

**Integration:** `dialog-overlay` uses FTXUI's `Modal(mainChild, dialogRenderer, &active)`. When `active` is true, the modal overlay captures all input. Dismissing returns focus to `input-bar`.

---

### `md-renderer` — Markdown → FTXUI Element

| Field | Value |
|-------|-------|
| **Class** | `a0::tui::MarkdownRenderer` in `src/tui/markdown_renderer.h/.cpp` |
| **Parent** | used inline by `msg-panel` when rendering assistant entries |
| **FTXUI type** | `Element` tree (not a `Component`) |
| **Purpose** | Converts markdown strings to FTXUI renderable elements via MD4C SAX parser. |

**Public API:**
```
render(md, streaming=false)       → full Element tree
renderInline(md)                  → inline only (no block-level)
```

**Rendering map:**
| Markdown | FTXUI Element |
|----------|---------------|
| H1-H2 | `bold` + `color(Color::White)` + `size(GREATER_THAN, ...)` |
| H3+ | `bold` |
| Paragraph | `text` wrapped |
| Code block | `dim` + `border` + `flex` |
| Inline code | `inverted` |
| Bold | `bold` |
| Italic | `dim` |
| Link | `underlined` + `color(Color::Blue)` |
| List | indented `text` with bullet |
| Strikethrough | `dim` |

---

### `clipboard` — System clipboard

| Field | Value |
|-------|-------|
| **Class** | free function `copyToClipboard(text)` in `src/tui/clipboard.h/.cpp` |
| **Parent** | called from `app`'s mouse-up handler |
| **Purpose** | Copy selected text to system clipboard. |

**Strategy (fallthrough):**
1. Write OSC 52 escape sequence: `ESC ] 52 ; c ; <base64> BEL`
2. If `WAYLAND_DISPLAY` set → `wl-copy`
3. Else if `xclip` exists → `xclip -selection clipboard`

---

## 3. Interaction Effects Matrix

| Trigger | `app` → sends | `status-bar` | `msg-panel` | `input-bar` | `dialog-stack` |
|---------|---------------|--------------|-------------|-------------|----------------|
| User types + Enter | `SubmitGoal` | setAgentState(Thinking) | append(User, text) + beginAssistant() | clear, disable | — |
| Ctrl+C | `Cancel` | setAgentState(Idle) | finalizeAssistant + append(System, "Interrupted") | enable, focus | — |
| `/sessions` | `ListSessions` | — | — | — | showList("Sessions") |
| `LlmToken` | — | — | appendOrUpdateAssistantText | — | — |
| `ToolStart` | — | setAgentState(Executing) | appendAssistantTool(Running) | disable | — |
| `ToolEnd` | — | setAgentState(Thinking) | updateLastAssistantTool(Completed) | — | — |
| `RoundComplete` | — | — | endCurrentAssistantText | — | — |
| `Complete` | — | setAgentState(Idle) | appendOrUpdateAssistantText + finalizeAssistant | enable, focus | — |
| `Error` | — | setAgentState(Idle) | appendOrUpdateAssistantText + finalizeAssistant + append(Error) | enable, focus | — |
| `SessionList` | — | — | — | — | showList with entries |
| List item selected | `ResumeSession` | — | — | — | dismiss |
| `SessionHistory` | — | setSessionId, setMsgCount | clear + flat append per message | — | dismiss |
| `/clear` | — | — | clear | — | — |
| `:q` / `/quit` | `Shutdown` | — | — | — | — |

## 4. Data Flow Reference

```
MPSC RECEIVE (core → TUI):
  AppCoreThread (bg thread)
    → evtSender.send(event)           ← writes eventfd + pushes to deque
    → wakeupFn()                      ← screen->Post(Task{})
    → FTXUI event loop wakes
    → AgentTui::drainEvents()         ← called each frame in run()
      → evtReceiver.drain()           ← reads eventfd, empties deque
      → xHandleCoreEvent(event)       ← variant switch
        → panel mutation methods       ← direct calls (same thread)
        → RequestAnimationFrame()      ← schedule re-render

USER INPUT (TUI → core):
  FTXUI Input / CatchEvent
    → xHandleSubmit(text)             ← main thread
      → expandPastePlaceholders
      → msg-panel->append(User, text)
      → msg-panel->beginAssistant()     ← creates single ┌─ Assistant entry
      → cmdSender.send(SubmitGoal{})  ← writes eventfd + pushes to deque
      → RequestAnimationFrame()
    → AppCoreThread drains command    ← bg thread's ppoll loop

EVENT DISPATCH (core → TUI):
  DrivenCore::tick() → AppCoreThread → evtSender → drainEvents
    ├── LlmToken    → xOnToken → appendOrUpdateAssistantText(asstIdx, accumText)
    ├── ToolStart   → xOnToolStart → appendAssistantTool(asstIdx, name, Running, args)
    ├── ToolEnd     → xOnToolEnd → updateLastAssistantTool(asstIdx, Completed/Failed, output)
    ├── RoundComplete → xOnRoundComplete → endCurrentAssistantText(asstIdx)
    │                  (emitted by DrivenCore when Complete arrives + pending tool calls)
    ├── Complete    → xOnComplete → appendOrUpdateAssistantText + finalizeAssistant(asstIdx)
    │                  (emitted by DrivenCore when core reaches Idle — final round)
    └── Error       → xOnError → appendOrUpdateAssistantText + finalizeAssistant(asstIdx)
```

## 5. Lessons Learned

### 5.1 Scrolling: one system, not two

The first implementation had a custom virtual window (`m_scrollTop` + `VISIBLE_ENTRIES=8`) AND FTXUI's `yframe` element. Two independent scroll systems fought for viewport control; the `yframe` reacted to FTXUI's internal cursor tracking (shifted by mouse focus changes), while `m_scrollTop` managed its own independent slider window. This caused "jump to top" on mouse move and broken PgUp/PgDn.

**Fix:** Remove the virtual window. Render ALL entries, let `yframe` clip the vbox. Use `ftxui::focus` on a single target entry to control scroll position. One system (`yframe` + `focus`) handles everything.

### 5.2 Scroll position context for scrollUp/scrollDown

When `autoScroll` is true, the `focusIndex` field may be stale (0, from initialization). A naive `scrollUp(n)` computes `max(0, focusIndex - n) = 0` — scrolling does nothing.

**Fix:** Derive the "current position" from the `autoScroll` context:
```
current = autoScroll ? total - 1 : focusIndex
focusIndex = max(0, current - n)
```
Apply the same pattern in `scrollDown`.

### 5.3 Tool call indices must survive across LLM turn boundaries

A single user goal may trigger multiple LLM responses: analysis (no tool calls) → tool calls → tool results. `xOnComplete` fires between turns. Clearing `m_toolCallIndices` on `xOnComplete` erased valid tool call indices for the pending turn.

**Fix:** Only clear `m_toolCallIndices` on new goal submit (`xHandleSubmit`) and interrupt (`xHandleInterrupt`). `xOnComplete` and `xOnError` leave the queue intact. As a safety net, the fallback path (queue empty) scans backwards for a matching Running entry before creating a duplicate.

### 5.4 Name-based updateToolCall fallback

When `m_toolCallIndices` is empty but a `ToolEnd` arrives (e.g., from a late tool response after a queue-clearing event), `appendToolCall` would create a duplicate entry. The first entry (from `ToolStart`) remained in Running state forever — showing `⏳ running` indefinitely.

**Fix:** `updateToolCall(name, state, output)` scans backwards through entries, finds the first Running Tool entry with matching name, and updates it in-place. If none is found, returns -1 and the caller does nothing (no duplicate created).

### 5.5 Tool arguments display

Arguments arrive in `ToolStart::arguments` (JSON string). Display them inline on the tool header line for debugging visibility:
```
┌─ Tool: read {"file_path": "/etc/passwd"}
```

Stored in `MessageEntry::toolArgs`, rendered in `xRenderEntry` by appending to the tool label. No separate args line needed.

### 5.6 PTY 4KB buffer deadlock

The kernel PTY buffer is 4096 bytes. The TUI writes ~10KB frames. If the test process sleeps without reading the PTY master, the buffer fills, the TUI blocks on `write()` to the slave, and the entire TUI event loop stalls. No more events are processed; no more frames are written.

**Symptoms:** `capture()` timeouts returning empty strings, test taking much longer than expected.

**Fix:** Never use bare `time.sleep()` to wait for TUI output. Always use a capture loop that drains the PTY at least every 500ms:
```python
deadline = time.monotonic() + timeout
while time.monotonic() < deadline:
    t = driver.capture(timeout=2)
    if t and "expected marker" in t:
        break
```

### 5.7 FTXUI Draw() optimization

FTXUI's `ScreenInteractive` compares the current rendered output with the previous frame. If unchanged, no bytes are written to stdout. This means `capture()` returns empty between idle frames — not because the TUI stalled, but because FTXUI correctly deduplicated the output.

**Fix:** Send a benign keystroke (e.g., type a space then backspace) to trigger a state change, forcing Draw() to emit a frame. Then capture.

### 5.8 `ftxui::reflect` box storage must outlive the render frame

`ftxui::reflect(Box&)` captures a reference to a box and fills it during rendering. If the box is a stack-local variable, storing a copy in a vector produces a **default-initialized (all-zeros)** value — the copy is made before FTXUI's layout engine fills the original. Click hit-testing against zero boxes never matches.

**Symptoms:** Click-to-toggle on tool children silently fails; `Box{x_min=0, x_max=0, y_min=0, y_max=0}` in all tool hits.

**Fix:** Store the `Box` directly in the vector element's storage (e.g., `toolHits[].box`) and pass a **reference** to that stable storage to `reflect`. The box is filled in-place by FTXUI and survives across frames because the vector owns the memory:

```cpp
// Wrong — box is a copy of stack-local
ftxui::Box box;
elems.push_back(element | ftxui::reflect(box));
m_impl->toolHits.push_back({entryIdx, ci, box});  // box = {0,0,0,0}

// Correct — box lives in the vector, reflect fills it in-place
m_impl->toolHits.push_back({entryIdx, ci, ftxui::Box{}});
elems.push_back(element | ftxui::reflect(m_impl->toolHits.back().box));
```

See `xRenderAssistant` in `message_panel.cpp`.

### 5.9 Ctrl+C must be handled in the outer CatchEvent when input is disabled

When the input bar is disabled (`setEnabled(false)`), `InputPanel::component()` returns a plain `Renderer("Waiting...")` that ignores all events. `Event::CtrlC` never reaches `setOnInterrupt`. If Ctrl+C is sent while a goal is processing, the interrupt handler never fires — the user sees "Thinking" indefinitely.

**Symptoms:** Ctrl+C during goal processing has no effect. Status bar stays on "Thinking". No "Interrupted" message appears.

**Fix:** Handle `Event::CtrlC` in `app`'s outer `CatchEvent` (in `agent_tui.cpp`) before it reaches any child component. The outer CatchEvent runs the handler regardless of child focus state:

```cpp
if (event == ftxui::Event::CtrlC) {
    xHandleInterrupt();
    return true;
}
```

The `Event::CtrlC` check is at the CatchEvent level, before the event is forwarded to children. This ensures interrupt works even when `input-bar` is disabled, a dialog is open, or focus is on any non-handling component.

### 5.10 Use `RoundComplete` for intermediate LLM rounds, `Complete` only for final

The `Complete` event fires for every LLM response — both when the model returns tool calls (intermediate) and when it returns final text. Previously the TUI had to infer which was which by checking `m_toolCallIndices` or `m_state`, which was fragile.

**Fix:** Add a `RoundComplete` event type to `AppCoreEvent` variant. In `DrivenCore::tick()`, after `xHandleLlmEvents` processes the events, convert `Complete` to `RoundComplete` when `m_state` is `ExecutingTools` (intermediate round). The final `Complete` passes through unchanged:

```cpp
for (auto& ev : events) {
    if (auto* c = std::get_if<mpsc::Complete>(&ev)) {
        if (m_state != CoreState::Idle) {
            ev = mpsc::RoundComplete{std::move(c->text)};
        }
    }
}
```

The TUI handles `RoundComplete` by sealing the current text child (`endCurrentAssistantText`), keeping the `┌─ Assistant` entry alive. `Complete` finalizes the entire assistant entry. This eliminated the need for `m_turnHadToolCalls` flag and the fragile state inference in the old `xOnComplete`.

### 5.11 Tool entries as children of the assistant entry

Originally tool calls were flat entries in the message panel. This caused ordering issues: after `RoundComplete` sealed the text and `ToolStart` appended a tool, the tool entry appeared AFTER the assistant entry (which was created earlier). The chronological order was wrong — tools should appear between text segments within a single assistant block.

**Fix:** Make assistant entries a container with `children` — `std::vector<MessageEntry>`. Text segments (`role=Assistant`) and tool calls (`role=Tool`) are appended as ordered children. `xRenderAssistant` iterates children in order, producing the correct visual:

```
┌─ Assistant                                    ← single entry
I'll look up the files...                       ← text child 0
🔧 read {"file_path":"."} ✅ completed           ← tool child 1
Here are the results I found.                   ← text child 2
```

This required rewriting all msg-panel API methods (`beginAssistant`, `appendOrUpdateAssistantText`, etc.) and the `app` event handlers.

### 5.12 Non-streaming `Complete` needs fallback content

When the mock server responds in non-streaming mode (no `stream` flag), no `LlmToken` events arrive — just a single `Complete` with the full text. With the old code, the synthetic `[thinking]\n` token provided accumulated text. After removing the `[thinking]` token, `m_streamingText` was empty when `Complete` arrived.

**Symptoms:** `xOnComplete` creates a child with empty content (since `appendOrUpdateAssistantText` uses `m_streamingText`). The assistant appears to have no response text.

**Fix:** In `xOnComplete`, use `fullOutput` (from `Complete.text`) when `m_streamingText` is empty:

```cpp
const std::string& text = m_streamingText.empty() ? fullOutput : m_streamingText;
m_messagePanel->appendOrUpdateAssistantText(m_assistantEntryIndex, text);
```

This handles both streaming (tokens in `m_streamingText`) and non-streaming (text in `fullOutput`) modes.

## 6. Design Constraints (NO list)

These are enforced by code review on every change to `src/tui/` files.

| # | Rule |
|---|------|
| 1 | NO includes of `src/` headers outside `src/tui/`, except `mpsc.h`, `hex_session_id.h`, `trace.h` |
| 2 | NO pointers/references to: `DrivenCore`, `DrivenProvider`, `LlmProvider`, `DeepSeekProvider`, `SkillManager`, `PersistenceStore`, `SqliteStore`, `CommandRunner`, `DependencyGraph`, `ToolState`, `ContainerManager`, `ComposeManager`, `DockerToolRunner`, `StreamRegistry`, `SessionContext`, `AgentCore` |
| 3 | NO direct persistence calls (no SQL, no `loadMessages`, no `createSession`) |
| 4 | NO `std::thread` creation |
| 5 | NO `SessionManager` class |
| 6 | NO direct LLM interaction (no provider calls, no curl, no API keys) |
| 7 | NO tool execution (no `CommandRunner`, no `SkillManager` dispatch) |
| 8 | NO direct core method calls of any kind |
| 9 | `CMakeLists.txt` must NOT link `a0_lib` or `persistence_lib` |
| 10 | MPSC events are read-only — received and rendered, never modified and sent back |
