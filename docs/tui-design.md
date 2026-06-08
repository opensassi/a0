# TUI Layout Reference

## 1. Screen Layout Overview

```
┌─ status-bar ─────────────────────────────────────────────┐
│  80380ff7│Thinking│b1: ✓│──│5 msgs                        │
├─ msg-panel (scrollable) ─────────────────────────────────┤
│                                                           
│  ┌─ You                                                   
│  read src/tool_state.cpp                                  
│                                                           
│  ┌─ Assistant                                             
│  [thinking]                                               
│                                                           
│  ┌─ Tool: read                                            
│  🔧 read  ✅ completed                                    
│  1: #include "tool_state.h"                               
│  2:                                                       
│  3: void ToolState::set(...                               
│                                                           
│  ┌─ Assistant                                             
│  Here is the content of `src/tool_state.cpp`:             
│  ```cpp                                                   
│  #include "tool_state.h"                                  
│  ...                                                      
│                                                           
│  ↑ 3 more                                                 
│                                                           
├─ input-bar ───────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────┐ │
│  │ type your message...                       ┃         │ │
│  └──────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────┘
```

The screen has three stacked regions: status bar at the top, scrollable message panel in the middle (flex-grow), fixed input bar at the bottom. Dialogs overlay the center region.

---

## 2. Component Catalog

### §2.1 `status-bar` — Top status line

**Position:** First line of the terminal, fixed height 1.

**Appearance:**

```
  80380ff7│Thinking│b1: ✓│──│5 msgs
  ^-------^ ^------^ ^---^   ^----^
  session   agent    b1      message
  prefix    state    status  count
```

- **Session prefix:** First 8 characters of the session UUID, dim white.
- **Agent state:** Colored label — `Idle` (green), `Thinking` (yellow), `Executing` (red), `Error` (red).
- **B1 status:** `b1: ✓` (green) when connected, `b1: --` (dim) when disconnected.
- **Message count:** `N msgs` — total messages in the current session.

**Transitions:**

| Event | Change |
|-------|--------|
| User submits a goal | `Idle` → `Thinking`, message count increments |
| LLM starts returning tool calls | `Thinking` → `Executing` |
| Tool execution completes | `Executing` → `Thinking` |
| Final response arrives | `Thinking` → `Idle` |
| Ctrl+C / error | Any → `Idle` |
| `b1` connects/disconnects | `b1: --` ↔ `b1: ✓` |
| Session loaded (resume) | prefix + count update |

---

### §2.2 `msg-panel` — Message history

**Position:** Between status-bar and input-bar. Fills all remaining vertical space.

**Behavior:** Scrollable list of messages. Older messages scroll up as new ones appear. When scrolled up, a `scroll-hint` shows how many entries are above/below the viewport.

---

#### §2.2.1 `msg-you` — User message

**Appearance:**
```
┌─ You
read src/tool_state.cpp
```

- Cyan `┌─ You` prefix on its own line
- Message text in plain text below the prefix
- Multi-line text shown as-is with line breaks

---

#### §2.2.2 `msg-assistant` — Assistant response

**Appearance:**

While streaming:
```
┌─ Assistant
[thinking]▌                    ← streaming indicator with cursor
```

Complete:
```
┌─ Assistant
Here is the content of `src/tool_state.cpp`:
 1: #include "tool_state.h"
 2:
 3: void ToolState::set(...)
```

- Green `┌─ Assistant` prefix
- Content is rendered from **Markdown** — headings, bold, italic, code blocks, lists, links all formatted
- Code blocks have a dimmed background border
- While streaming: shows `[thinking]` with a cursor, updated token-by-token
- When complete: final formatted text is displayed, no cursor

---

#### §2.2.3 `msg-tool` — Tool execution result

**Appearance (collapsed — default):**
```
🔧 read {"file_path": "/"}  ✅ completed
```

**Appearance (expanded):**
```
🔧 read {"file_path": "/"}  ✅ completed
1: #include "tool_state.h"
2:
3: void ToolState::set(...)
```

- Single-line header with 🔧 wrench icon, tool name, arguments (dimmed), status icon + text
- Click the header line to toggle collapse/expand
- Collapsed by default — output area hidden
- Expanded: stdout shown below the header (dimmed, monospace)
- Stderr shown separately if present (red-tinted)

**Lifecycle within a single tool block:**

| Phase | Header display | Output area |
|-------|----------------|-------------|
| Pending | 🔧 `name` ⏳ pending | (empty) |
| Running | 🔧 `name` ⏳ running | (empty or partial) |
| Completed | 🔧 `name` ✅ completed | full stdout/stderr |
| Failed | 🔧 `name` ❌ failed | error output |

---

#### §2.2.4 `msg-system` — System notification

**Appearance:**
```
┌─ System
Session resumed: abc123
```

- Yellow `┌─ System` prefix
- Informational messages from the system (not from user or LLM)

---

#### §2.2.5 `msg-error` — Error message

**Appearance:**
```
┌─ Error
Failed to connect to API
```

- Red `┌─ Error` prefix
- Error description in red text

---

#### §2.2.6 `tool-spinner` — Running indicator

**Appearance:**
```
Pending:    🔧 read
Running:    🔧 read  ⏳ running
Completed:  🔧 read  ✅ completed
Failed:     🔧 read  ❌ failed
```

The icon + status line updates automatically as the tool transitions through its lifecycle. Each tool block shows one of the four states above at any given time.

---

*(Removed — scrollbar provided by `vscroll_indicator`)*

---

### §2.3 `input-bar` — Bottom input field

**Position:** Last line(s) of the terminal, fixed height (minimum 3).

**Appearance (enabled):**
```
┌────────────────────────────────────────────────────────┐
│ type your message...                                   │
└────────────────────────────────────────────────────────┘
```

- Bordered text box
- Placeholder text `type your message...` in dim when empty
- `┃` cursor at end of typed text

**Appearance (disabled, waiting for response):**
```
Waiting for response...
```
- Dim text centered, no border
- Input is blocked

**User interaction:**
- Type text freely (multiline support with Shift+Enter)
- **Enter** (bare): submits the message
- **Shift+Enter**: inserts a newline in the input
- **Ctrl+C**: interrupts the current agent processing
- **Up/Down arrows**: navigate input history (previous submissions)
- **Paste detection**: pasted text > 20 chars is collapsed to `[PASTED #N]` placeholder, expanded on submit

---

### §2.4 `dialog` — Modal overlay

**Position:** Center of the screen, overlays `msg-panel`. Background behind the dialog is dimmed.

**Types:**

| Dialog | Purpose | Contents |
|--------|---------|----------|
| **Help** | `/help` or `?` | Keybinding reference table |
| **Sessions** | `/sessions` | List of recent sessions (UUID, date, message count), selectable |
| **Confirm** | (internal) | Yes/No prompt with title and message |

**User interaction:**
- **Escape**: dismiss the dialog
- **Enter**: confirm selection / activate selected item
- **Up/Down**: navigate session list items
- Dialogs block interaction with the `msg-panel` and `input-bar` underneath

---

### §2.5 `copy-feedback` — Selection copy indicator

**Behavior:** When the user selects text by dragging the mouse across `msg-panel` content and releases, the selected text is automatically copied to the system clipboard. A brief `Copied!` flash appears in the `status-bar` for 1-2 seconds.

**Selection mechanism:** Mouse drag inside `msg-panel` triggers text selection tracking. On mouse release, FTXUI's selection API retrieves the rendered text under the drag coordinates, and the clipboard module copies it via OSC 52 / wl-copy / xclip.

---

## 3. Source File Mapping

| UI Component | File | Key Symbol |
|-------------|------|-----------|
| status-bar | `src/tui/status_bar.h` | `StatusBar` class |
| msg-panel | `src/tui/message_panel.h` | `MessagePanel` class |
| msg-you | `src/tui/styles.h` | `roleDecorator(User)` |
| msg-assistant | `src/tui/styles.h`, `src/tui/markdown_renderer.h` | `roleDecorator(Assistant)`, `MarkdownRenderer` |
| msg-tool / tool-spinner | `src/tui/message_panel.h` | `appendToolCall`, `updateToolCall` |
| msg-system | `src/tui/styles.h` | `roleDecorator(System)` |
| msg-error | `src/tui/styles.h` | `roleDecorator(Error)` |
| scroll-hint | `src/tui/message_panel.cpp` | (render logic inside `MessagePanel::Render()`) |
| input-bar | `src/tui/input_panel.h` | `InputPanel` class |
| dialog (help) | `src/tui/dialog_manager.cpp` | `DialogManager::showHelp()` |
| dialog (sessions) | `src/tui/dialog_manager.cpp` | `DialogManager::showList()` |
| copy-feedback | `src/tui/clipboard.h`, `src/tui/agent_tui.cpp` | `copyToClipboard`, mouse-up handler |
| styles/colors | `src/tui/styles.h` | `roleLabel`, `roleDecorator`, `MessageRole` enum |
| layout assembly | `src/tui/agent_tui.cpp` | `xBuildLayout()` |

---

## 4. Interaction Quick Reference

| User does | What changes on screen |
|-----------|----------------------|
| Types + Enter | `input-bar` clears. `msg-you` appears with typed text. `msg-assistant` appears with `[thinking]`. `status-bar` shows `Thinking`. |
| Agent streams tokens | `msg-assistant` text updates incrementally. Each chunk appears as it arrives. |
| LLM calls a tool | `status-bar` shows `Executing`. `msg-tool` block appears with `🔧 name  ⏳ running`. |
| Tool finishes | `msg-tool` changes to `🔧 name  ✅ completed`. Output text appears below. `status-bar` shows `Thinking`. |
| Final response arrives | `msg-assistant` finalizes (cursor removed). `status-bar` shows `Idle`. Input becomes enabled. |
| Ctrl+C | Current streaming freezes. `msg-error` shows "Interrupted". `status-bar` → `Idle`. Input re-enabled. |
| `/sessions` | `dialog` appears with session list. Background dimmed. |
| Select a session | `dialog` dismisses. `msg-panel` loads that session's history. `status-bar` updates prefix + count. |
| `/help` | `dialog` shows keybinding reference. |
| `/clear` | All messages removed from `msg-panel`. |
| `:q` / Ctrl+Q | TUI exits. Terminal restored. |
| Mouse drag on messages | Text selected under drag. On release, `Copied!` flashes in `status-bar`. Text copied to clipboard. |
| Paste (>20 chars) | `[PASTED #N]` appears in `input-bar`. Full text is expanded on submit. |
| Up/Down in input | Cycles through previously submitted messages. |
