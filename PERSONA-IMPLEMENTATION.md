# Persona System Implementation — Session Summary

## What Was Built (This Session)

### New files created (11)

| File | Purpose |
|------|---------|
| `personas/schema.json` | Draft-07 JSON Schema validating `persona.json` (name, description, promptFile, skills, tools) |
| `personas/system/software-engineer/persona.json` | Software Engineer persona config: `skills: ["system_task-manager"]`, `tools: ["system_fs_read", "system_fs_glob", "system_fs_grep", "system_bash_bash"]` |
| `personas/system/software-engineer/prompt.md` | Full system prompt (174 lines) — planning & task management agent, no execution |
| `personas/system/product-designer/persona.json` | Product Designer persona config: empty skills/tools |
| `personas/system/product-designer/prompt.md` | Product design prompt (343 lines) — non-technical conversational designer |
| `src/personas.h` | Persona data structures (`PersonaManifest`, `Persona`) + `PersonaLoader` class |
| `src/personas.cpp` | `PersonaLoader` implementation: walks system/local/github namespaces, parses persona.json, reads prompt.md |
| `src/personas.spec.md` | Spec for PersonaLoader, PersonaManifest, Persona, lifecycle diagram |
| `personas/README.md` | Developer guide for persona authors (8 sections, 295 lines) |
| `test/unit/test_personas.cpp` | 14 PersonaLoader unit tests (loadAll, getPersona, case sensitivity, error handling) |
| `test/unit/test_task_system.cpp` | 21 Task handler + SqliteStore task method tests |

### Existing files modified (29)

| File | Change |
|------|--------|
| `skills/system/task-manager/skill.json` | 4 system tools (`systemTool: true`, `default: true`) with JSON Schema parameters matching prompt signatures |
| `skills/system/meta/skill.json` | Tool names: `show_skills`→`show-skills`, `show_skill_tools`→`show-skill-tools`, `tools_for_prompt`→`tools-for-prompt` |
| `skills/system/git/skill.json` | Tool name `rev_parse`→`rev-parse`; prompt dependencies `system-git-*`→`system_git_*`; prompt names `start_session`→`start-session`, `finish_session`→`finish-session` |
| `src/skills/skill_manager.cpp` | `parseQualifiedName`/`buildQualifiedName`: separator changed from `-` to `_`. Special `github_` prefix handling. `find('_')` (first) for comp-name split. Wildcard matching uses `parseQualifiedName` instead of `rfind('_')`. `resolveName`/`xCollectChain`: checks known ns prefix instead of `find('_')`. `buildDispatchTable`: `rfind('_')`, `xSplit(qn, '_')`. `missingHandlers`: `_` instead of `-`. `executeToolStreaming` wildcard: same fix. |
| `src/skills/skill_loader.cpp` | `xIndexKey`: `"system_"`/`"local_"`/`"github_"` prefix format |
| `src/base_prompt.h` | `buildBasePrompt()` takes optional `personaName` parameter (default: `"software-engineer"`) |
| `src/base_prompt.cpp` | Replaced `loadTemplate()` with `PersonaLoader`-based resolution. Loads persona prompt + applies `{{BUILD_HASH}}`/`{{OS_INFO}}`/`{{CWD}}` substitution. Fallback to hardcoded error string if persona not found. |
| `src/persistence/persistence_store.h` | Added `Task` struct + `createSessionRootTask`, `getSessionRootTask`, `addTask`, `removeTask`, `listTasks`, `updateTaskPriority`, `getTask` pure virtuals + NullStore stubs |
| `src/persistence/sqlite_store.h` | Added 7 task method declarations |
| `src/persistence/sqlite_store.cpp` | Added `task` + `session_tasks` tables (tree structure: `root_task_id`, `parent_task_id`), indexes, 7 method implementations |
| `src/system_handlers.h` | Added 4 task manager handler declarations (`xAddTask`, `xRemoveTask`, `xListTasks`, `xSetTaskPriority`) |
| `src/system_handlers.cpp` | 4 handler implementations for task CRUD + update help text for renamed tools |
| `src/driven_core.h` | Added `m_personaName`, `m_personaSkills`, `m_personaTools` members; `setPersona()`, `setPersonaSkills()`, `setPersonaTools()` |
| `src/driven_core.cpp` | `xBuildToolSchemas()` always filters by persona skills/tools (no fallback to all tools). `xStartLlmRequest()` persists system prompt + tool schemas once per session. |
| `src/app_core_thread.h/.cpp` | Threads persona name + skills/tools from constructor to `DrivenCore` |
| `src/main.cpp` | Added `--persona` CLI flag (default: `"software-engineer"`). Loads persona skills/tools via `PersonaLoader` after skill load, passes to `AppCoreThread`. Registers 4 task manager handlers in `AgentStack` constructor. Root task creation + `ToolState::session_id` in both `cmdRun` and `cmdTui`. |
| `src/driven_core.spec.md` | Updated: added `setPersona/Skills/Tools`, `m_personaName/Skills/Tools`, `m_systemPromptPersisted`; removed `xBuildInitialMessages` |
| `src/persistence/persistence_store.spec.md` | Updated: added `Task` struct, `SessionRow`, `loadSessions`, `saveSessionSystemPrompt`, all 7 task methods |
| `src/persistence/sqlite_store.spec.md` | Updated: added `task`/`session_tasks` tables, `system_prompt` columns, all task methods |
| `src/persistence/technical-specification.md` | Updated: added task tables, system prompt columns, task testing requirements, root task creation in integration |
| `src/system_handlers.spec.md` | Updated: added 4 task manager handler declarations, `_` separator, camelCase alias docs |
| `src/main.spec.md` | Updated: `--persona` flag, `AppCoreThread` + MPSC architecture, `_` handler keys, `PersonaLoader` |
| `src/base_prompt.spec.md` | Updated: `PersonaLoader`-based resolution, `personaName` parameter |
| `src/app_core_thread.spec.md` | Updated: `personaName/Skills/Tools` constructor params and members, new MPSC commands |
| `src/skills/skills.spec.md` | Updated: `subModules` field, `HandlerContext` const ref, 5-param `executeToolWithMeta` |
| `src/skills/skill_manager.spec.md` | Updated: `executeToolStreaming`, `setRecordingSession`, `toolState()`, scope prefixes |
| `src/skills/technical-specification.md` | Updated: persona integration notes, built-in skills section |
| `technical-specification.md` | Updated: persona system overview, task management, new CLI flags, personas/ file layout |
| `test/unit/test_driven_core_persistence.cpp` | Extended: 6 persona filtering tests (by skills, by tools, combined, empty), `NoPersona_EmptySchemas` |
| `test/e2e/test_agent_e2e.py` | Added: `TestAgentPersona` class with 3 E2E tests (default, explicit, invalid no-crash) |
| `CMakeLists.txt` | Registered `test_personas` and `test_task_system` test executables |
| `session_context.cpp` | `"system_git_rev-parse"` (hyphenated tool name) |
| `agent_core.cpp` | `"system_meta_tools-for-prompt"` (hyphenated tool name) |
| `dependency_graph.cpp` | Reader/writer prefixes: `"system_fs_*"`, `"system_meta_"` |

### Test files modified

| File | Change |
|------|--------|
| `test/unit/test_skill_manager.cpp` | Updated qualified names to `_` separator + `-` in tool names throughout |
| `test/unit/test_dependency_graph.cpp` | Updated classifier prefixes |
| `test/unit/test_pipeline_execution.cpp` | Updated tool invocation names |
| `test/unit/test_skill_pipeline.cpp` | Updated handler registration keys |
| `test/unit/test_skill_resolver.cpp` | Updated handler registration keys |
| `test/unit/test_skill_args.cpp` | Updated handler registration keys |
| `test/unit/test_external_a0.cpp` | Updated wildcard handler keys |
| `test/unit/test_system_tools_e2e.cpp` | Updated qualified names |
| `test/unit/test_dependency_resolver.cpp` | Updated component/dependency names |
| `test/unit/test_agent_tool_calls.cpp` | Updated component/dependency names |
| `test/unit/test_agent_integration.cpp` | Updated component/dependency names |
| `test/unit/test_a0_launcher.cpp` | Updated skill names |
| `test/tui/mock/mock_persistence_store.h` | Added 7 task method stubs |
| `test/unit/test_session_context.cpp` | `buildBasePrompt` test — no change needed (default parameter backward compatible) |

---

## Key Decisions Made

### Qualified name format: `ns_skill-name_tool-name`

The qualified name separator was changed from `-` to `_`. Hyphens are reserved for use within segment values (e.g., `task-manager`, `add-task`). Underscores are ONLY the separator between namespace, skill, and tool name — never within a segment. This makes `parseQualifiedName` unambiguous: `find('_')` after the namespace prefix always finds the correct split boundary.

### Tool names use `-` not `_`

Previously, tool names like `add_task`, `show_skills`, `rev_parse` used underscores internally. With `_` now the separator, these were renamed to `add-task`, `show-skills`, `rev-parse`. This ensures no ambiguity in `parseQualifiedName`.

### `parseQualifiedName` uses first `_` (not last) for component-name split

Since component names don't contain `_` (they use `-`), the first `_` after the namespace prefix separates the component from the tool name. This correctly handles tool names that contain `-` (like `add-task`, `set-task-priority`).

### `github_<user>` namespace special casing

The `github_<user>` namespace contains `_` internally. `parseQualifiedName` detects the `github_` prefix and searches for the second `_` as the namespace-name separator.

### Wildcard matching uses `parseQualifiedName`, not `rfind('_')`

The original wildcard matching used `rfind('_')` to find what was assumed to be the component-name boundary. With tool names containing `-` this is correct. The fix uses `parseQualifiedName` to decompose the qualified name and reconstruct the wildcard as `ns + "_" + comp + "_*"`, which correctly handles any tool name.

### Persona skills/tools filtering

`xBuildToolSchemas()` always filters tool schemas by the active persona's `skills` and `tools` lists. When both are empty, no tools are exposed. The `tools` list allows cherry-picking individual tools from a skill without including the entire skill's toolset. There is no fallback to loading all default tools.

### Task tree storage (SQLite)

Tasks are stored as a tree using `root_task_id` and `parent_task_id` columns. Root tasks are self-referencing (`root_task_id = parent_task_id = id`). A `session_tasks` table links sessions to their root task. The `0` convention in LLM-facing tools (`parent_task_id=0` → use session root) is resolved by the handler via `getSessionRootTask()`.

### System prompt persisted in SQLite

The system prompt (from the selected persona's `prompt.md` with variable substitution) and the full tool schema definitions are persisted on the `session` table once per session. This enables deterministic replay without needing to reload the persona at replay time.

### Default persona is software-engineer

When no `--persona` flag is specified, the "software-engineer" persona is used. This persona replaces the former `prompts/base.md` as the default system prompt. The `prompts/` directory was removed since it's no longer referenced.

---

## Architecture

### Persona Lifecycle

```
personas/system/{name}/persona.json  ──→ PersonaLoader::xParseManifest()
personas/system/{name}/prompt.md     ──→ PersonaLoader::xReadPrompt()
                                                │
                                                ▼
                                        PersonaLoader stores
                                        vector<Persona>
                                                │
                                 main.cpp: PersonaLoader::getPersona(name)
                                                │
                                ┌───────────────┴──────────────────┐
                                ▼                                  ▼
                  buildBasePrompt()                  DrivenCore::setPersonaSkills()
                  returns persona prompt             DrivenCore::setPersonaTools()
                  with {{VAR}} substitute                        │
                                │                                ▼
                                ▼                    xBuildToolSchemas()
                  xStartLlmRequest()                filters schemas by
                  sends as system role              allowed skills/tools
```

### Session Init Flow

```
main.cpp
  ├─ AgentStack constructor
  │   ├─ xRegisterSystemHandlers()    ← bash, fs, git, meta wildcards
  │   └─ registerHandler() x4        ← task-manager (add-task, remove-task, etc.)
  │
  ├─ skillMgr.loadAll()              ← loads skills/system/*/skill.json
  ├─ PersonaLoader::loadAll()        ← loads personas/system/*/persona.json
  ├─ createSessionRootTask()         ← creates root task + session_tasks link
  ├─ AppCoreThread(personaName/Skills/Tools)
  │   └─ xRun()
  │       ├─ DrivenCore::setPersona(personaName)
  │       └─ DrivenCore::setPersonaSkills/Tools()
  │           └─ xBuildToolSchemas() filters by persona config
```

### Tool Schema Filtering

```
xBuildToolSchemas()
  ├─ Build allowed skill prefixes from m_personaSkills (e.g. "system_task-manager_")
  ├─ For each loaded manifest component:
  │   └─ For each default tool with parameters:
  │       └─ Include if QN matches skill prefix or m_personaTools
  ├─ Add individual tools from m_personaTools via getTool()
  └─ Filter dispatch table entries:
      └─ Only entries whose skill prefix or qualified name
         matches persona config
```

---

## Test Results

| Suite                       | Status     | Failures |
| --------------------------- | ---------- | -------- |
| C++ unit tests (34 targets) | 34/34 PASS | —        |
| Agent E2E tests (13 tests)  | 13/13 PASS | —        |

