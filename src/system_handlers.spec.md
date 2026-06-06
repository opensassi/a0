# System Handlers Spec

## 1. Overview

Free-standing C++ functions that implement all built-in system tools. Previously static methods on `SystemToolRegistry`, now stand-alone functions in the `a0` namespace. Each accepts `const json& params` (and optional dependencies) and returns `HandlerResult`. Handlers are registered onto `SkillManager` at startup via `SkillManager::registerHandler()`.

**Source files:** `src/system_handlers.h`, `src/system_handlers.cpp`

**Registration:** All handlers are registered in `main.cpp` via `xRegisterSystemHandlers()`, which calls `SkillManager::registerHandler(qn, lambda)` for each handler. Qualified names use `_` as the namespace/component/tool separator.

## 2. Component Specifications

```cpp
namespace a0 {

// Core filesystem + execution handlers
HandlerResult xBash(const json& params);
HandlerResult xRead(const json& params);
HandlerResult xGlob(const json& params);
HandlerResult xGrep(const json& params);
HandlerResult xEdit(const json& params);
HandlerResult xWrite(const json& params);

// Git handler (subcommand extracted by wildcard dispatch)
HandlerResult xGitCommand(const std::string& subcommand, const json& params);

// Meta handlers (require SkillManager)
HandlerResult xShowSkills(const json& params, a0::skills::SkillManager* skillMgr);
HandlerResult xShowSkillTools(const json& params, a0::skills::SkillManager* skillMgr);

// Task manager handlers (require PersistenceStore)
HandlerResult xAddTask(const json& params, a0::persistence::PersistenceStore* db);
HandlerResult xRemoveTask(const json& params, a0::persistence::PersistenceStore* db);
HandlerResult xListTasks(const json& params, a0::persistence::PersistenceStore* db);
HandlerResult xSetTaskPriority(const json& params, a0::persistence::PersistenceStore* db);

} // namespace a0
```

## 3. Registration Pattern

| Pattern | Key | Example |
|---------|-----|---------|
| Core tool | `ns_comp_tool` | `system_fs_read`, `system_bash_bash` |
| Git wildcard | `ns_comp_*` | `system_git_*` (subcommand in `HandlerContext::subcommand`) |
| Meta tool | `ns_comp_tool` | `system_meta_show-skills` |
| Task manager | `ns_comp_tool` | `system_task-manager_add-task` |

## 4. Handler Details

### Core Handlers

| Handler | Input key | Description |
|---------|-----------|-------------|
| `xBash` | `command`, `[timeout]`, `[workdir]` | Executes shell command. Also accepts `snake_case` keys (`file_path`, `old_string`) and `camelCase` aliases (`filePath`, `oldString`). |
| `xRead` | `file_path`/`filePath`, `[offset]`, `[limit]` | Reads file with line numbers or lists directory entries. Detects binary files. |
| `xGlob` | `pattern`, `[path]` | Recursive file pattern matching. Excludes node_modules, .git, etc. |
| `xGrep` | `pattern`, `[path]`, `[include]` | Regex content search. Excludes binary and large files. |
| `xEdit` | `file_path`/`filePath`, `old_string`/`oldString`, `new_string`/`newString`, `[replace_all]`/`[replaceAll]` | Exact string replacement. |
| `xWrite` | `file_path`/`filePath`, `content` | Creates parent directories, writes file. |

### Git Handler

| Handler | Dispatch | Description |
|---------|----------|-------------|
| `xGitCommand` | `system_git_*` via `HandlerContext::subcommand` | Builds `git <subcommand> --flag=value` from params. |

### Meta Handlers

| Handler | Dependencies | Description |
|---------|-------------|-------------|
| `xShowSkills` | `SkillManager*` | Navigates skill tree by path (e.g. `/system`, `/local`). |
| `xShowSkillTools` | `SkillManager*` | Lists tools from manifests by component path. |

### Task Manager Handlers

| Handler | Dependencies | Description |
|---------|-------------|-------------|
| `xAddTask` | `PersistenceStore*` | Creates task under parent (0 = session root). Sets all fields from params. |
| `xRemoveTask` | `PersistenceStore*` | Removes leaf task. Fails if task has children. |
| `xListTasks` | `PersistenceStore*` | Lists children of parent (0 = session root). |
| `xSetTaskPriority` | `PersistenceStore*` | Updates task sort priority. |

## 5. Testing Requirements

| Handler | Test | Input | Expected |
|---------|------|-------|----------|
| `xBash` | Missing command | `{}` | `"ERROR: missing required..."` |
| `xBash` | Echo command | `{"command":"echo hi"}` | `"hi\n"` |
| `xRead` | Missing file_path | `{}` | `"ERROR: missing required..."` |
| `xEdit` | Single replace | Valid file + old/new | `"Edit applied successfully"` |
| `xWrite` | New file | Valid path + content | `"Wrote file successfully"` |
| `xGlob` | Glob pattern | Pattern + path | Matching files listed |
| `xGrep` | Pattern match | Pattern + path | Matching lines |
| `xGrep` | Invalid regex | `"[invalid"` | `"ERROR: invalid regex pattern..."` |
| `xAddTask` | With parent=0 | `{parent_task_id: 0, description: "t"}` | `{"task_id": N}` |
| `xAddTask` | With explicit parent | Same + parent_task_id=N | Child task created |
| `xAddTask` | All fields | Full params | All fields round-trip |
| `xRemoveTask` | Leaf task | `{task_id: N}` | `{"removed": true}` |
| `xRemoveTask` | Has children | Task ID with children | Error message |
| `xListTasks` | Existing children | `{_session_id: N}` | Numbered task list |
| `xSetTaskPriority` | Update | `{task_id: N, priority: 5}` | `{"updated": true}` |
