# Persona System Developer Guide

## 1. Overview

A **persona** is a bundle of a **system prompt** (`prompt.md`) and a **manifest** (`persona.json`) that defines the agent's identity and capabilities. Personas control two things:

- **What the agent says** — the system prompt loaded from `prompt.md` with variable substitution
- **What the agent can do** — the set of skills and tools exposed to the LLM via schema filtering

Personas are organized in a three-tier namespace matching the skills system:

| Namespace | Path | Read-only | Purpose |
|-----------|------|-----------|---------|
| `system` | `personas/system/` | Yes | Built-in personas shipped with a0 |
| `local` | `personas/local/` | No | User-created or agent-created personas |
| `github_<user>` | `personas/github_<user>/` | Yes | Installed from GitHub |

**Lifecycle:** load → select → prompt + filter

```
personas/{ns}/{name}/persona.json ──▶ PersonaLoader
personas/{ns}/{name}/prompt.md    ──▶         │
                                              ▼
                                      buildBasePrompt()
                                      + xBuildToolSchemas()
                                              │
                          ┌───────────────────┴───────────────────┐
                          ▼                                       ▼
                    System prompt text                   Tool schema filter
                    (sent as LLM system role)            (only allowed tools exposed)
```

---

## 2. Persona Manifest (persona.json)

Every persona is defined by a `persona.json` file. The full JSON Schema is at `personas/schema.json`.

### Required fields

```json
{
  "name": "software-engineer",
  "description": "A skilled software engineer persona",
  "promptFile": "prompt.md"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Persona identifier (case-insensitive for lookup) |
| `description` | string | Human-readable summary |
| `promptFile` | string | Path to the markdown prompt file, relative to the persona directory |

### Optional fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `skills` | array of strings | `[]` | Skill references (`ns_comp`) — all tools from these skills are included |
| `tools` | array of strings | `[]` | Individual qualified tool names (`ns_comp_tool`) to include |

### Full skeleton

```json
{
  "name": "my-persona",
  "description": "What this persona does",
  "promptFile": "prompt.md",
  "skills": ["system_task-manager", "system_fs"],
  "tools": ["system_bash_bash", "local_utils_format"]
}
```

---

## 3. Skill/Tool Filtering

When a persona declares `skills` or `tools`, only those matching tools are exposed to the LLM. Filtering is always active — there is no "load everything" fallback.

### How skills work

A `skills` entry is a two-part qualified reference to a skill component: `ns_comp`. All tools loaded from that component's manifest with `"default": true` and a `"parameters"` JSON Schema become available.

```json
{
  "skills": ["system_task-manager"]
}
```

This exposes all default tools from the `system/task-manager` skill: `add-task`, `remove-task`, `list-tasks`, `set-task-priority`.

### How tools work

A `tools` entry is a three-part qualified tool name: `ns_comp_tool`. Individual tools can be cherry-picked without including their entire parent skill.

```json
{
  "tools": ["system_fs_read", "system_fs_glob", "system_fs_grep"]
}
```

This exposes only `read`, `glob`, and `grep` from the `system/fs` skill, without `write` or `edit`.

### Combined resolution

When both `skills` and `tools` are present, the union is exposed:

```json
{
  "skills": ["system_task-manager"],
  "tools": ["system_fs_read", "system_fs_glob", "system_fs_grep", "system_bash_bash"]
}
```

Result: `add-task`, `remove-task`, `list-tasks`, `set-task-priority`, `read`, `glob`, `grep`, `bash` — approximately 8 tool schemas.

### Empty lists

If both `skills` and `tools` are empty or absent, **no tool schemas are loaded**. The LLM receives an empty tool list. This is useful for purely conversational personas that don't need tool access.

### Determining the qualified name

To find the qualified name for a tool, look at its position in the skill tree:

- Skill: `skills/system/task-manager/skill.json`
- Tool name in manifest: `"name": "add-task"`
- Namespace: `system`
- Component: `task-manager`
- Qualified name: `system_task-manager_add-task`

---

## 4. Prompt Files (prompt.md)

The `prompt.md` file contains the system prompt that is sent to the LLM as the system role message. It defines the agent's behavior, constraints, and knowledge.

### Variable substitution

| Variable | Source | Example |
|----------|--------|---------|
| `{{BUILD_HASH}}` | `BuildIdentity::binarySha1()` | `abc123def456` |
| `{{OS_INFO}}` | `uname()` sysname + release + machine | `Linux 6.2.0 x86_64` |
| `{{CWD}}` | `getcwd()` | `/home/user/project` |

Variables are substituted at runtime each time `buildBasePrompt()` is called.

### Writing an effective prompt

- Describe the agent's **role** and **purpose** in the first paragraph
- List **available tools** with their function signatures so the LLM knows what it can call
- Define **constraints** — what the agent should never do
- Provide **guidelines** for how to structure responses (planning, task breakdown, verification)

### Example excerpt

```markdown
# a0 Agent Prompt: Planning & Task Management

You are **a0**, a planning and task management agent for software engineering.
You **do not write code, edit files, run tests, or perform any execution actions**.

You have access to the following **tools**:

- `add_task(description, detailed_plan, automated_verification, human_verification, priority)`
- `remove_task(task_id)`
- `list_tasks()`
- `set_task_priority(task_id, priority)`
```

---

## 5. Building a Persona from Scratch

### Step 1: Create the directory

```bash
mkdir -p personas/system/my-persona
```

### Step 2: Write the prompt file

Create `personas/system/my-persona/prompt.md`:

```markdown
# My Custom Persona

You are a specialized assistant for...

## Available tools

- `read(file_path)`
- `glob(pattern)`

## Guidelines

...
```

### Step 3: Create the manifest

Create `personas/system/my-persona/persona.json`:

```json
{
  "name": "my-persona",
  "description": "A specialized assistant",
  "promptFile": "prompt.md",
  "skills": ["system_fs"],
  "tools": ["system_bash_bash"]
}
```

### Step 4: Test the persona

```bash
a0 --persona my-persona run --prompt "hello"
```

Or in TUI mode:

```bash
a0 --persona my-persona tui
```

To verify your persona's tool filter is working, export the session and inspect the `_meta.tool_definitions` field:

```bash
a0 session export --session-id <uuid> | jq 'select(._meta) | .tool_definitions | length'
```

---

## 6. Built-in Personas

### software-engineer

The default persona (activated when `--persona` is not specified). A planning and task management agent focused on high-assurance software development.

- **Skills:** `system_task-manager`
- **Tools:** `system_fs_read`, `system_fs_glob`, `system_fs_grep`, `system_bash_bash`
- **Prompt:** Instructs the agent to create atomic tasks with automated verification, never execute code directly

### product-designer

A non-technical conversational designer persona for product design discussions.

- **Skills:** _(none)_
- **Tools:** _(none)_
- **Prompt:** Conversational product design guidance without tool access

---

## 7. Advanced Topics

### Namespace resolution

Personas in `system/` are read-only (not overridable). Personas in `local/` are writable — the agent can create new personas here. The `github_<user>/` namespace allows distribution of personas via GitHub.

### Persona names are case-insensitive

`getPersona("Software-Engineer")` finds `software-engineer` because lookup normalizes to lowercase.

### Error handling

- Missing `persona.json`: directory is skipped with a warning
- Missing `promptFile`: persona loads with an empty prompt string
- Invalid JSON in `persona.json`: directory is skipped with a warning
- Persona name not found: `buildBasePrompt()` returns `"ERROR: persona \"<name>\" not found"` which is sent as the system prompt

### Testing your persona

Unit tests for persona loading live in `test/unit/test_personas.cpp`. To add a test fixture for a new persona:

```cpp
TEST_F(PersonaLoaderTest, MyPersona_LoadsCorrectly) {
    writePersona("system", "my-persona", "custom prompt");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    auto p = loader.getPersona("my-persona");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->prompt, "custom prompt");
}
```

---

## 8. Integration Points

| Component | What it does with personas |
|-----------|---------------------------|
| `PersonaLoader` (`src/personas.cpp`) | Walks namespace directories, parses manifests, reads prompts |
| `buildBasePrompt()` (`src/base_prompt.cpp`) | Loads persona prompt, substitutes `{{VAR}}` placeholders |
| `DrivenCore::xBuildToolSchemas()` (`src/driven_core.cpp`) | Filters tool schemas using persona skills/tools |
| `main.cpp` | Parses `--persona` flag (default `"software-engineer"`), loads manifest, passes to `AppCoreThread` |
| `AppCoreThread::xRun()` (`src/app_core_thread.cpp`) | Calls `core.setPersona/Skills/Tools()` on startup |
