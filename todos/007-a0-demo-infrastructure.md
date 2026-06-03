# Todo: a0 Infrastructure for Demo-Video Pipeline

**Created:** 2026-06-03
**Session:** session-demo skill planning — demos/prompt.md pipeline analysis
**Priority:** high

## Context

The `skills/local/session-demo` skill (Todo 008) needs three capabilities that a0 doesn't currently provide: a user-prompt mechanism for blocking user interaction, a session-query tool for generating content from stored sessions, and Playwright bridge video recording. These must be built first (or in parallel with workarounds).

Design reference: session-demo skill plan from 2026-06-03 (conversation with agent about `skills/local/session-demo` pipeline).

---

### 1. User Prompt Mechanism (`system:meta:user_prompt`)

Stages 05 and 06 of the demo pipeline require user selection of audio/video segments. Currently no tool can ask the user a question and wait for a response.

**Required:**
- A C++ system handler registered as `system:meta:user_prompt`
- Accepts `{message: string, options?: string[]}` as params
- In REPL mode: prints message to stdout, reads a line from stdin, returns `{response: "..."}`
- In IPC mode (via b1→c2): sends a `user_prompt` IPC message, blocks until `prompt_reply` arrives
- For `a0 run --skill` mode: reads `--skill-arg` params for non-interactive defaults (or returns a "no stdin" error)

**Files:**

| File | Change |
|------|--------|
| `src/system_handlers.cpp` / `.h` | New `xUserPrompt()` handler function returning `HandlerResult` |
| `src/main.cpp` | Register `system:meta:user_prompt` in `xRegisterSystemHandlers()` |
| `agent_interfaces.h` | No change needed — `HandlerResult` already supports output strings |
| `src/agent_core.h` / `.cpp` | May need `m_promptFd` or callback wiring for IPC blocking mode |

**Design questions:**
- Should the handler be synchronous (block until response) or callback-based?
- For IPC mode: how does the handler signal it's waiting? (Could use the existing `user_prompt` IPC message type defined in c2 spec.)
- For `a0 run` mode: should it reject with a clear error, or read from a known file path?

**Test plan:**

| Test | Verification |
|------|-------------|
| REPL: handler called with `{message: "Continue?"}` | Prints message, reads stdin, returns `{response: "y"}` |
| IPC: handler called while connected via b1 | Sends `user_prompt` IPC, blocks, returns on `prompt_reply` |
| No stdin: handler called in `a0 run` | Returns error string or checks skill-args for auto value |

---

### 2. Session Fork/Query Tool (`system:session:query`)

The Stage 00 script (`00-demo-video-assets-from-session.cjs`) generates content (title, narratives, features) from a stored session. It currently uses `opencode run '<prompt>' -s <session-id> --fork` which doesn't exist in a0.

**Required:**
A tool that:
- Takes `{sessionId: string, prompt: string}` as params
- Loads the session's messages from the SQLite persistence store
- Constructs a system prompt with the session conversation history as context
- Runs the LLM inference with this context
- Returns the LLM response text
- Respects token limits (truncates session history if needed)

**Exposed as:**
- C++ system handler: `system:session:query`
- CLI subcommand: `a0 session query <session-id> <prompt>` (for use in shell scripts)

**Files:**

| File | Change |
|------|--------|
| `src/persistence/persistence_store.h` | Add `loadSessionContext(sessionId, maxTokens) → string` method |
| `src/persistence/sqlite_store.cpp` | Implement: query messages table, concatenate role+content, trim to token budget |
| `src/system_handlers.cpp` / `.h` | New `xSessionQuery()` handler: load context → call InferenceProvider → return result |
| `src/main.cpp` | Register `system:session:query` handler; add `a0 session query` CLI subcommand |
| `test/unit/test_system_handlers.cpp` | Tests with seeded SQLite session data |

**Token budget heuristics:**
- Session messages from SQLite, newest-first, until ~80% of model context window
- Reserve 20% for the query prompt + response
- Each message: `role: content\n` — count characters / 4 as rough token estimate

**Test plan:**

| Test | Verification |
|------|-------------|
| Query with seeded session (3 messages) | Response contains content from session history |
| Query with empty session ID | Returns error "session not found" |
| Query with very long session | Truncates within token limit, no crash |
| CLI: `a0 session query <id> "summarize"` | Prints summary to stdout |

---

### 3. Playwright Bridge `record_video` Action

`04-record-clip.cjs` currently uses direct `require('playwright')` for video recording because the Playwright bridge has no recording action. This creates a second Playwright dependency and bypasses the bridge infrastructure.

**Required:**
- Add a `record_video` action to `scripts/playwright-bridge.js`
- Creates a new browser context with `recordVideo: {dir: outputDir, size: {width, height}}`
- Navigates to the target URL
- Optionally executes timed scroll anchors during recording
- Waits for the specified duration, then closes the recording context
- Returns the recorded video file path and metadata

**API:**

```
POST / {"action":"record_video", "url":"...", "durationSecs":30,
         "outputDir":"/path/to/artifacts", "width":1920, "height":1080,
         "anchors": [{"timeSecs":0.5, "scrollPct":0}, ...]}
→ {"ok":true, "file":"clip-00-opening.webm", "duration":30.5, "sizeMB":4.2}
```

**Files:**

| File | Change |
|------|--------|
| `scripts/playwright-bridge.js` | New `record_video` case in `handleAction()` switch |
| `skills/local/playwright/skill.json` | Add `browser_record_video` tool definition |
| `scripts/browser.sh` | No change — already passes through all actions |

**Implementation notes:**
- Create a new ephemeral context per recording (each recording is isolated)
- The `recordVideo` option in Playwright writes WebM files to the context's video dir
- After the recording duration + buffer (0.5s), close the context and locate the video file
- Handle navigation failures gracefully (record what's visible)
- Support `headless: true/false` from existing bridge config

**Edge cases:**

| Case | Behavior |
|------|----------|
| URL fails to load | Record blank/error page, return file with error flag |
| Duration exceeds max (600s) | Reject with error |
| Scroll anchor time > duration | Clamp to duration - 0.5s |
| Multiple concurrent recordings | Each gets own context, isolated |
| No `outputDir` | Use bridge's CWD |

---

## Order

1. `system:meta:user_prompt` — needed for stages 05/06 user interaction
2. `system:session:query` — needed for stage 00 content generation from session
3. Playwright `record_video` — needed for stage 04 clean recording (04 can use direct Playwright as workaround)

Items 1 and 2 can be done in parallel. Item 3 depends on understanding of the bridge architecture.

## Dependencies

- SQLite persistence layer (exists) for session query
- IPC message types `user_prompt` / `prompt_reply` (defined in c2 spec, partially implemented)
- Handler registry + `registerHandler()` / `executeTool()` (exists via SkillManager)
- Playwright bridge (exists at `scripts/playwright-bridge.js`)
