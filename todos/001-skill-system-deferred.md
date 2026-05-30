# Todo: Skill System Integration — Deferred Items

**Created:** 2026-05-30  
**Session:** system-design + session-evaluation porting  
**Priority:** medium  

## Items

### 1. Export via Persistence Store (SQLite) instead of JSONL

Currently `exportSession()` in `JsonLinesLogger` reads from `logs/<id>.jsonl` files. The persistence layer (`PersistenceStore`/`SqliteStore`) records structured session data in SQLite under `.a0/db/sessions.db`. The export pipeline should read from the SQLite store instead, which provides richer data (tool call IDs, arguments, results, timestamps).

**Implementation sketch:**
```cpp
// In persistence_store.h add:
bool exportSession(int64_t sessionDbId, const std::string& outputPath) const;

// In sqlite_store.cpp:
// JOIN messages table, write as structured JSON array with
// tool_calls_json parsed into structured form.
```

The `--export-session` CLI flag in `main.cpp` would then use the persistence store instead of `JsonLinesLogger::exportSession()`.

**Files:**
- `src/persistence/persistence_store.h` — add `exportSession()`
- `src/persistence/sqlite_store.h/cpp` — implement SQLite export query
- `src/main.cpp` — switch from logger export to persistence store export
- `skills/local/opensassi/session-evaluation/bin/export_session.sh` — update to accept/store richer data if needed

---

### 2. `{{tool_call:...}}` Args Preservation + Dispatch Registration

The current `{{tool_call:name args...}}` pass 3 in `expandPrompt()` strips the template wrapper and extracts the short name but **discards the args string**. For tool calls that need parameter data (like `{{tool_call:export_session slug="<slug>" session="{{SESSION_ID}}"}}`), the args must be preserved, parsed into a `json` object, and registered in the dispatch table alongside the tool name.

**Current behavior (pass 3):**
```cpp
// Strips {{tool_call:name args}} → extracts short name
// Discards args completely
// No dispatch registration
```

**Required behavior:**
```
Parse args as key="value" pairs (same regex as pass 2)
Store {shortName, qualifiedName, args} in a dispatch registration table
At dispatch time (processGoal tool-calling loop), pass the pre-registered args
  or merge them with LLM-provided args
```

**Key design question:** How should pre-registered args interact with LLM-provided args? Options:
- Pre-registered args are defaults, LLM args override
- Pre-registered args are required, LLM args are additional
- Split: some args are fixed by the prompt, others are dynamic

**Files:**
- `src/skill_runner.cpp` — pass 3: parse and store args
- `src/agent_core.h/cpp` — extend dispatch table to hold optional args
- `src/skill_runner.h` — carry `m_globalVars` accessible during pass 3

---

### 3. Title-Slug Flow from Inference Through Export

The `export` command needs a title slug (e.g. `2026-05-30-skill-porting`). Currently:
- `generate` creates one during evaluation and holds it in context
- `export` needs to retrieve it

The flow should be:
1. `generate` outputs evaluation with a `Session ID` field containing the slug
2. The agent extracts and stores the slug (in context memory or a global var)
3. `export` reads the stored slug

**Implementation options:**
- Store slug as a global var: `{{EXPORT_SLUG}}` set by the `generate` prompt
- Pass slug explicitly when calling `export`
- Extract slug from the last `generate` output by parsing the `Session ID:` line

---

### 4. GitHub Install/Publish Machinery

`skills/local/opensassi/*` skills are developed locally. Eventually they need to be published to GitHub and installed via `a0 skill install github:user/repo`. This requires:

- `SkillManager::install()` implementation (currently a no-op stub in `skill_manager.cpp:xInstallFromGit`)
- Git clone + manifest validation + version archiving + GC
- Potentially a `a0 skill publish` command that bundles a `local/` skill and pushes to GitHub

**Files:**
- `src/skills/skill_manager.cpp` — implement `xInstallFromGit()`
- `src/main.cpp` — `a0 skill install/publish` CLI commands

---

### 5. `mock-api` Mode for LLM-Independent Testing

The session-evaluation `export` command and the system-design diagram generation commands all require LLM inference. A `--mock-api` flag exists but the mock server isn't set up in tests. The `test/e2e/mock_deepseek_server.py` exists but isn't integrated with the test runner.

Extend the synthetic test framework to optionally run against a mock API for full `execute()` pipeline testing (expand → LLM → validators → export).
