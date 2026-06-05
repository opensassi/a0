# System Handlers Spec

## 1. Overview

Free-standing C++ functions that implement all built-in system tools. Previously static methods on `SystemToolRegistry`, now stand-alone functions in the `a0` namespace. Each accepts `const json& params` and returns `HandlerResult`. Handlers are registered onto `SkillManager` at startup via `SkillManager::registerHandler()`.

**Source files:** `src/system_handlers.h`, `src/system_handlers.cpp`

**Registration:** All handlers are registered in `main.cpp` via `xRegisterSystemHandlers()`, which calls `SkillManager::registerHandler(qn, lambda)` for each handler.

**Wildcard dispatch:** Git handler uses wildcard key `system-git-*`. `SkillManager::executeToolWithMeta()` sets `params["_subcommand"]` to the tool name after the hyphen.

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

// Git handler
HandlerResult xGitCommand(const std::string& subcommand, const json& params);

// Meta handlers (require SkillManager only)
HandlerResult xShowSkills(const json& params, a0::skills::SkillManager* skillMgr);
HandlerResult xShowSkillTools(const json& params, a0::skills::SkillManager* skillMgr);

// NOTE: xToolsForPrompt was removed from the active codebase.
// It required InferenceProvider* which has been replaced by the async LlmProvider.
// The function body remains in system_handlers.cpp as reference for future
// implementation of a static tool listing or async analysis.

} // namespace a0
```

## 3. Registration Pattern

| Pattern | Key | Example |
|---------|-----|---------|
| Core tool (hyphen-separated) | `ns-comp-tool` | `system-fs-read` |
| Git wildcard | `ns-comp-*` | `system-git-*` (subcommand extracted from `_subcommand` param) |
| Meta tool | `ns-comp-tool` | `system-meta-show_skills` |

## 4. Handler Details

### Core Handlers

| Handler | Input key | Description |
|---------|-----------|-------------|
| `xBash` | `command`, `description`, `[timeout]`, `[workdir]` | Executes shell command. Rejects git and docker commands with error. |
| `xRead` | `file_path`, `[offset]`, `[limit]` | Reads file with line numbers or lists directory entries. Detects binary files. |
| `xGlob` | `pattern`, `[path]` | Recursive file pattern matching. Excludes node_modules, .git, etc. |
| `xGrep` | `pattern`, `[path]`, `[include]` | Regex content search. Excludes binary and large files. |
| `xEdit` | `file_path`, `old_string`, `new_string`, `[replace_all]` | Exact string replacement. Errors on not-found or multiple matches. |
| `xWrite` | `file_path`, `content` | Creates parent directories, writes file. |

### Git Handler

| Handler | Dispatch | Description |
|---------|----------|-------------|
| `xGitCommand` | `system-git-*` via `_subcommand` | Builds `git <subcommand> --flag=value` from params. |

### Meta Handlers

| Handler | Dependencies | Description |
|---------|-------------|-------------|
| `xShowSkills` | `SkillManager*` | Navigates skill tree by path (e.g. `/system`, `/local`). |
| `xShowSkillTools` | `SkillManager*` | Lists tools from manifests by component path. |

## 5. Testing Requirements

| Handler | Test | Input | Expected |
|---------|------|-------|----------|
| `xBash` | Missing command | `{}` | `"ERROR: missing required..."` |
| `xBash` | Echo command | `{"command":"echo hi"}` | `"hi\n"` |
| `xBash` | Git rejection | `{"command":"git status"}` | `"ERROR: git commands must use..."` |
| `xRead` | Missing file_path | `{}` | `"ERROR: missing required..."` |
| `xRead` | File not found | `{"filePath":"/no/such"}` | `"ERROR: file not found"` |
| `xEdit` | Single replace | Valid file + old/new | `"Edit applied successfully"` |
| `xWrite` | New file | Valid path + content | `"Wrote file successfully"` |
| `xGlob` | Glob pattern | Pattern + path | Matching files listed |
| `xGrep` | Pattern match | Pattern + path | Matching lines |
| `xGrep` | Invalid regex | `"[invalid"` | `"ERROR: invalid regex pattern..."` |
| `xGitCommand` | Status | Subcommand=status | Git status output |
