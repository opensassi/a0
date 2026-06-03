# Skill System Developer Guide

## 1. Overview

A **skill** is a bundle of **tools** (executable operations) and **prompts** (LLM templates) distributed as a `skill.json` manifest. Skills are organized in a three-tier namespace:

| Namespace | Path | Read-only | Purpose |
|-----------|------|-----------|---------|
| `system` | `skills/system/` | Yes | Built-in tools shipped with a0 |
| `local` | `skills/local/` | No | Agent-created or user-installed skills |
| `github_<user>` | `skills/github_<user>/` | Yes | Installed from GitHub |

**Lifecycle:** load → resolve → execute → validate

```
skill.json  ──▶ SkillLoader ──▶ in-memory index ──▶ resolve by qualified name
                                                          │
                                          executeTool / executePrompt
                                                          │
                                                    ToolRunner / InferenceProvider
```

---

## 2. Skill Manifest (skill.json)

Every skill is defined by a `skill.json` file. The full JSON Schema is at `schema.json`.

### Required fields

```json
{
  "name": "fs",
  "version": "1.0.0",
  "description": "Filesystem tools — read, search, edit, and write files"
}
```

### Optional sections

| Section | Description |
|---------|-------------|
| `tools` | Array of tool definitions (system handlers or subprocess commands) |
| `prompts` | Array of prompt templates with parameter substitution |
| `args` | Documentation for CLI arguments this skill accepts |
| `subModules` | List of sub-component directories to auto-load |

### Full skeleton

```json
{
  "name": "my_skill",
  "version": "1.0.0",
  "description": "What this skill does",
  "args": [ ... ],
  "tools": [ ... ],
  "prompts": [ ... ],
  "subModules": [ ... ]
}
```

---

## 3. Defining Tools

A tool is either a **system tool** (C++ handler registered at startup) or a **command tool** (subprocess invoked via bash). The `systemTool` boolean discriminates.

### System tool (C++ handler)

System tools are fast C++ functions registered on `SkillManager` via `registerHandler()` in `main.cpp`. They are declared in `skill.json` with `systemTool: true`.

**system/bash/skill.json:**
```json
{
  "name": "bash",
  "description": "Executes a given bash command in a persistent shell session",
  "systemTool": true,
  "default": true,
  "parameters": {
    "type": "object",
    "properties": {
      "command": { "type": "string", "description": "The command to execute" },
      "description": { "type": "string", "description": "Clear description (5-10 words)" },
      "timeout": { "type": "number", "description": "Timeout in ms", "default": 120000 }
    },
    "required": ["command", "description"]
  }
}
```

The C++ handler is registered in `main.cpp`:
```cpp
mgr.registerHandler("system-bash-bash", [](const json& p, const HandlerContext&) {
    return a0::xBash(p);
});
```

### Command tool (subprocess)

Command tools run a shell command. Parameters are passed via stdin or args depending on `inputMode`.

**local/opensassi/system_design/skill.json:**
```json
{
  "name": "extract_artifacts",
  "description": "Extract mermaid fenced blocks and D3 HTML from .spec.md files",
  "command": "bash scripts/extract_artifacts.sh",
  "inputMode": "args",
  "dockerImage": "ubuntu:22.04",
  "aptDependencies": ["bash", "coreutils"]
}
```

### inputMode: stdin vs args

| Mode | How params are passed |
|------|----------------------|
| `"stdin"` | Params JSON is serialized and piped to the command's stdin |
| `"args"` | Each top-level key becomes `--key=value`; key `"_"` becomes a positional arg |

### Wildcard dispatch

The `system-git-*` wildcard handles all git subcommands through one handler. The `subCommand` field can override the CLI subcommand name:

```json
{
  "name": "rev_parse",
  "description": "Pick out and massage parameters",
  "systemTool": true,
  "subCommand": "rev-parse"
}
```

The wildcard handler receives `ctx.subcommand` set to the resolved tool name:
```cpp
mgr.registerHandler("system-git-*", [](const json& p, const HandlerContext& ctx) {
    return a0::xGitCommand(ctx.subcommand, p);
});
```

### 2-part alias resolution

When a tool's name matches its component name, it can be invoked with just 2 parts: `system-bash` automatically resolves to `system-bash-bash`.

### Docker tools

Command tools can run inside Docker containers:
```json
{
  "command": "bash scripts/validate_all.sh",
  "dockerImage": "ubuntu:22.04",
  "trustLevel": "MEDIUM",
  "aptDependencies": ["nodejs", "npm"],
  "useContainerPool": true
}
```

| Field | Default | Description |
|-------|---------|-------------|
| `dockerImage` | `""` (host) | Docker image (e.g., `ubuntu:22.04`) |
| `trustLevel` | `"MEDIUM"` | `HIGH` (shared container), `MEDIUM` (shared), `LOW` (per-tool isolation) |
| `useContainerPool` | `true` | `false` = ephemeral `docker run --rm` |
| `aptDependencies` | `[]` | APT packages to install inside the container |

### Streaming flag

Tools that support streaming output should declare `streaming: true`:

```json
{
  "name": "tail_logs",
  "description": "Tail a log file with streaming output",
  "command": "tail -f /var/log/syslog",
  "inputMode": "stdin",
  "streaming": true,
  "timeoutSecs": 30
}
```

When `streaming: true`, the tool can be invoked via `SkillManager::executeToolStreaming()` which returns a `StreamHandle` with chunked callbacks. System tools fall through to synchronous mode (all output in one callback).

### Parameters field (LLM function calling)

The `parameters` field is a JSON Schema object that describes the tool's input for LLM function calling. Only tools with `parameters` are exposed to the LLM:

```json
{
  "name": "read",
  "systemTool": true,
  "default": true,
  "parameters": {
    "type": "object",
    "properties": {
      "file_path": { "type": "string", "description": "Absolute path to the file" },
      "offset": { "type": "number", "description": "Starting line", "default": 1 },
      "limit": { "type": "number", "description": "Max lines", "default": 2000 }
    },
    "required": ["file_path"]
  }
}
```

When `default: true`, the tool is included in the LLM's base anchor schema (shown on every request). When `default: false` or absent, it must be recommended by `tools_for_prompt`.

---

## 4. Defining Prompts

A prompt is an LLM template that can reference parameters, call tools, and chain base prompts.

### Inline prompt vs promptFile

```json
{
  "name": "greet",
  "description": "Says hello",
  "prompt": "Hello, {{name}}!"
}
```

Or load from a file:
```json
{
  "name": "generate",
  "description": "Generate a session evaluation",
  "promptFile": "prompts/generate.md"
}
```

`promptFile` is resolved relative to the `skill.json` directory.

### Parameter substitution

`{{key}}` placeholders are replaced from the invocation params:

```json
{
  "name": "echo",
  "prompt": "User said: {{message}}"
}
```

```bash
$ a0 run --skill local-test-echo --params '{"message":"hello"}'
# → LLM receives: "User said: hello"
```

### Global variables

These variables are available in every prompt template:

| Variable | Source | Example |
|----------|--------|---------|
| `{{SESSION_ID}}` | AgentCore init | `"d4a7f2c1b3e809f7"` |
| `{{A0_DIR}}` | AgentCore init | `".a0"` |
| `{{PROJECT_DIR}}` | CWD at startup | `"/home/user/project"` |
| `{{A0_SRC_DIR}}` | External repo clone | `".a0/external/a0"` |
| `{{<skill-arg>}}` | `--skill-arg` flag | See section 9 |

### Eager tool calls

`{{tool:name key="value"}}` executes a tool BEFORE the LLM is called, and substitutes its output into the prompt:

```json
{
  "name": "audit_report",
  "prompt": "Files in src/:\n{{tool:system-fs-glob pattern=\"src/*/\"}}\n\nAnalyze the above."
}
```

The `system-fs-glob` tool runs first, and its output replaces the `{{tool:...}}` placeholder before the prompt reaches the LLM. Within the same component, short names work (e.g. `validate_all` resolves to `local-system_design-validate_all`).

### Tool call substitution

`{{tool_call:qualified:name}}` inserts just the short name (for LLM function calling schemas):

```
Available: {{tool_call:system:fs:read}}, {{tool_call:system:bash}}, {{tool_call:system:git:status}}
```

Resolves to: `Available: read, bash, status`

### Chain field — composing prompts

Prompts can inherit from base prompts via the `chain` array. Chains are resolved recursively — base prompts' chains are resolved first, then concatenated with double-newlines.

```json
{
  "prompts": [
    {
      "name": "system_design_base",
      "prompt": "You are a system design expert..."
    },
    {
      "name": "load_spec",
      "chain": ["system_design_base"],
      "dependencies": ["system-fs-glob", "system-fs-read"],
      "prompt": "Read the spec tree at {{path}}"
    }
  ]
}
```

The resolved `load_spec` prompt becomes:
```
You are a system design expert...

Read the spec tree at {{path}}
```

Chains can reference cross-component prompts by qualified name:
```json
"chain": ["local-comp_a-base_prompt"]
```

### Dependencies

Prompts declare which tools they depend on as qualified names:

```json
{
  "prompt": "Search {{directory}} for 'log'",
  "dependencies": [
    "system-fs-glob",
    "system-fs-grep",
    "local-file_utils-list_files"
  ]
}
```

The `DependencyResolver` checks these before execution. Missing dependencies cause an error.

---

## 5. Validators

Validators are post-LLM processing tools that transform or validate the LLM output before returning it to the agent. They are specified on prompts:

```json
{
  "name": "generate_d3_animation",
  "validators": [
    {"toolName": "local-system_design-test_artifacts"},
    {"toolName": "local-system_design-verify_artifact"}
  ]
}
```

### Sequential mode (default)

Output pipes from one validator to the next, like a Unix pipeline:

```
LLM output → validator[0] → output → validator[1] → final result
```

If any validator returns `"ERROR: ..."`, the result is prefixed with `"VALIDATOR_ERROR: "`.

### Parallel mode

When `parallelValidators: true`, all validators receive the same input independently and run via `DependencyGraph` batching:

```json
{
  "name": "validate_output",
  "parallelValidators": true,
  "validators": [
    {"toolName": "local-validators-auth"},
    {"toolName": "local-validators-length"}
  ]
}
```

If any validator fails, all errors are reported. If all pass, the first validator's output is returned.

---

## 6. Execution Ordering (DependencyGraph)

When multiple tool calls arrive from a single LLM response, the `DependencyGraph` class determines safe execution order based on the filesystem as a single shared resource.

### Classification table

| Class | Tools | Parallelism |
|-------|-------|-------------|
| **READER** | `system-fs-read`, `system-fs-glob`, `system-fs-grep`, `system-meta-*` | Run in parallel with each other |
| **WRITER** | `system-fs-write`, `system-fs-edit` | Run one at a time, never with readers |
| **READ_WRITE** | `system-bash-bash`, `system-git-*`, all command tools | Run one at a time, after all readers and writers complete |

### Batch building

```
Input invocations:  [read, write, read, glob, edit, bash]
                          │
                    DependencyGraph::buildBatches()
                          │
Batch 0 (parallel): [read, read, glob]       ← all readers
Batch 1 (serial):   [write]                  ← writer
Batch 2 (serial):   [edit]                   ← writer
Batch 3 (serial):   [bash]                   ← read_write
```

### maxParallel CLI flag

```bash
a0 --max-parallel=8    # default is 4
```

Controls how many subprocess tools within a batch can run concurrently via `CommandRunner::runAll`.

---

## 7. Stateful Tools (ToolState)

Tools are stateless by default — each invocation is independent. When a tool needs to share state across invocations (e.g., a browser session, database connection), use `ToolState`.

### HandlerContext

```cpp
struct HandlerContext {
    std::string subcommand;     // wildcard suffix
    ToolState* toolState;       // per-session state bag
};
```

### Basic operations

```cpp
HandlerResult xMyHandler(const json& params, const HandlerContext& ctx) {
    // Write
    ctx.toolState->set("session_key", json("value"));

    // Read
    json val = ctx.toolState->get("session_key");

    // Check
    if (ctx.toolState->has("other_key")) { ... }

    // Remove
    ctx.toolState->remove("temp_key");

    // Clear entire state
    ctx.toolState->clear();

    return {"ok"};
}
```

### Lifecycle

`ToolState` is cleared at the start of each `processGoal()` call. This means state is scoped to a single goal processing session, which may span multiple LLM turns and tool invocations.

### Example: Playwright bridge

The Playwright skill stores the bridge's connection info:
```cpp
ctx.toolState->set("bridge_port", json(3100));
```

Subsequent tools read it:
```cpp
int port = ctx.toolState->get("bridge_port").get<int>();
```

---

## 8. Streaming Execution

Tools with `streaming: true` can be invoked via `SkillManager::executeToolStreaming()`, which returns a `StreamHandle` and calls `onChunk(data, direction)` from a background thread.

### How it works

- **Command tools**: delegate to `ToolRunner::runStreaming()` which uses `CommandRunner::runStreaming()` to fork a subprocess and read stdout/stderr chunk by chunk.
- **System tools**: fall through to synchronous mode. The handler runs once and all output is delivered in a single `onChunk("stdout")` call.

### Route selection

The `SkillRunner::executeStreaming()` method routes to `SkillManager::executeToolStreaming()` when the params contain `_tool` and `streaming: true`:

```cpp
json params;
params["_tool"] = "local-playwright-browser_snapshot";
params["streaming"] = true;
```

---

## 9. Passing CLI Arguments to Skills

Skill-level CLI arguments are passed via the `--skill-arg` flag:

```bash
a0 --skill-arg=playwright-headless=false \
   --skill-arg=playwright-unsafe-local
```

### Name convention

The key is `<skill-name>-<arg-name>`:

```json
// In skills/local/playwright/skill.json:
"args": [
  {"name": "headless", "type": "boolean", "default": true, "description": "..."},
  {"name": "unsafe-local", "type": "boolean", "default": false, "description": "..."}
]
```

```bash
# Runtime key is <skill>-<arg>
a0 --skill-arg=playwright-headless=false
```

### Reading args in handlers

```cpp
HandlerResult xBrowserLaunch(const json& params, const HandlerContext& ctx) {
    bool headless = true;
    if (ctx.toolState) {
        auto v = ctx.toolState->get("args:playwright-headless");
        if (!v.is_null()) headless = v.get<bool>();
    }
    // ...
}
```

Args are also available as global vars in prompt templates: `{{PLAYWRIGHT_HEADLESS}}`.

---

## 10. Docker Compose Support

Skills can bring up multi-service Docker environments using `docker-compose.yml`.

### Transient mode (default)

In the prompt, reference a compose file. The stack starts before `execute()` and stops immediately after:

```json
{
  "name": "db_test",
  "composeFile": "docker-compose.yml",
  "prompt": "Run queries against the PostgreSQL test database"
}
```

### Persistent mode

For environments that must live across multiple tool calls (e.g., a browser automation session), use persistent mode in C++ code:

```cpp
m_composeMgr->startPersistent("playwright", "docker-compose.yml", skillsDir);
// ... multiple tool calls ...
m_composeMgr->stopPersistent("playwright");
```

Persistent stacks are not stopped by individual `execute()` calls. Only an explicit `stopPersistent()` tears them down.

---

## 11. Startup Integration

### External a0 repo clone

At startup, `AgentCore::init()` clones `https://github.com/opensassi/a0` into `.a0/external/a0` (configurable via `--external-repo`). This provides access to tool scripts:

```bash
a0 --external-repo https://github.com/myfork/a0
```

The path is available as a global variable `{{A0_SRC_DIR}}` and in `ToolState["A0_SRC_DIR"]`. Currently always checks out `main` HEAD. Version-aligned checkout is a future enhancement.

### Available global template variables

```
{{SESSION_ID}}    — hex session UUID
{{A0_DIR}}        — .a0 root directory
{{PROJECT_DIR}}   — CWD at startup
{{A0_SRC_DIR}}    — path to external a0 clone
{{<skill-arg>}}   — any --skill-arg value
```

---

## 12. Testing Patterns

### Schema validation

Every `skill.json` is validated against `schema.json` at load time. The test `test_skill_schema.cpp` verifies all skill files validate.

### DependencyGraph batching

`test_dependency_graph.cpp` tests `classifyTool()` and `buildBatches()` — verifying READER/WRITER/READ_WRITE classification and correct batch ordering.

### Pipeline execution with mocks

`test_pipeline_execution.cpp` creates mock C++ handlers that record call order and timing, then runs them through `DependencyGraph::executeBatches()` to verify ordering and error propagation.

### ToolState

`test_tool_state.cpp` tests thread-safe set/get/has/remove/clear with concurrent access.

### E2E via Playwright

`test/e2e/test_playwright_e2e.sh` starts c2 + playwright-bridge, launches a headless browser, navigates to c2, takes snapshots, evaluates JS, and closes — all via the bridge API.

---

## 13. Real-World Examples

### system/fs — Filesystem tools

Six system tools (read, glob, grep, edit, write, no prompts). Minimal skill — tools only, used as the foundational filesystem interface. The `default: true` flag on all tools means they're included in every LLM request.

### system/git — Git command wildcard dispatch

Uses `system-git-*` wildcard to handle all git subcommands through one C++ handler. The `subCommand` field maps `rev_parse` → `rev-parse` for hyphens. Three prompts: `start_session`, `finish_session`, `sync` — each with dependencies on the git tools they invoke.

### system/bash — Bash execution

Single tool with `systemTool: true`. The `parameters` field includes `command`, `description`, `timeout`, `workdir` — providing the LLM with a structured function-calling schema.

### system/meta — Skill/tool introspection

Three meta-tools: `show_skills` (browse skill tree), `show_skill_tools` (browse tool definitions), `tools_for_prompt` (analyze user intent and recommend tools). These require `SkillManager*` and `InferenceProvider*` access, so their handlers are registered with captures in `main.cpp`.

### local/opensassi/system_design — Spec generation

Most complex skill: 5 command tools (Docker-based for artifact extraction/testing), 18 prompts with chaining, validators, eager tool calls. Uses `system_design_base` as a shared persona prompt chained by all sub-prompts. Each generation prompt has `validate_all` as a validator.

### local/opensassi/session_evaluation — Session export

1 command tool (`export_session` with `timeoutSecs: 60`), 3 prompts. Shows a simple skill with file-based output.

### local/playwright — Browser automation

22 command tools for browser automation, 2 prompts (e2e test agent persona + workflow). Key patterns:
- **Stateful**: stores bridge URL in ToolState
- **CLI args**: documents `headless` and `unsafe-local` in `args` array
- **Docker**: designed to run inside a Docker container with `docker-compose.playwright.yml`
- **Scripts**: tools delegate to `playwright-bridge.js` via `browser.sh` CLI shim

---

## Appendix: Key Design Decisions

### Why no parallelism enum on tools?

Manual parallelism scopes (`"none"`, `"component"`, `"global"`) are fragile in an open ecosystem — skill authors may not anticipate interaction effects. Instead, the `DependencyGraph` class uses a hardcoded classification table (READER/WRITER/READ_WRITE) based on known system tools. The entire project filesystem is treated as a single shared state. This guarantees safe ordering without per-tool declarations.

### Why no per-tool resource declarations?

All filesystem writes go through `system-fs-write` and `system-fs-edit`. Since these are known system tools, the classification table covers them. Custom command tools default to READ_WRITE (conservative). This avoids requiring every skill author to declare what resources their tool accesses.

### Why no transformer on validators yet?

The `ValidatorBinding::transform` field was declared in the struct (`std::optional<std::string> transform`) but never implemented. It was intended for future JSONPath binding to extract a subset of the output for the next validator. Deferred until a concrete use case demands it.

### Why Node.js for browser automation?

Playwright is a Node.js library that wraps Chromium's CDP protocol. A C++ equivalent would require thousands of lines of CDP protocol code. The `playwright-bridge.js` daemon (~50 lines) is a thin pass-through — it runs once per session, never per call, so there is no per-operation Node.js overhead.
