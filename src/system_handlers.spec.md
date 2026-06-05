# System Handlers Spec

## 1. Overview

Free-standing C++ functions that implement all built-in system tools. Previously static methods on `SystemToolRegistry`, now stand-alone functions in the `a0` namespace. Each accepts `const json& params` and returns `HandlerResult`. Handlers are registered onto `SkillManager` at startup via `SkillManager::registerHandler()`.

**Source files:** `src/system_handlers.h`, `src/system_handlers.cpp`

**Registration:** All handlers are registered in `main.cpp` via `xRegisterSystemHandlers()`, which calls `SkillManager::registerHandler(qn, lambda)` for each handler.

**Wildcard dispatch:** Git handler uses wildcard key `system:git:*`. `SkillManager::executeToolWithMeta()` sets `params["_subcommand"]` to the tool name after the last colon.

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

// Meta handlers (require SkillManager + InferenceProvider)
HandlerResult xShowSkills(const json& params, a0::skills::SkillManager* skillMgr);
HandlerResult xShowSkillTools(const json& params, a0::skills::SkillManager* skillMgr);
HandlerResult xToolsForPrompt(const json& params,
                              a0::skills::SkillManager* skillMgr,
                              InferenceProvider* provider);

} // namespace a0
```

## 3. Registration Pattern

Registration in `main.cpp` follows these conventions:

| Pattern | Key | Example |
|---------|-----|---------|
| Core tool (3-part) | `ns:comp:tool` | `system:fs:read` |
| Component-alias tool | `ns:comp` | `system:bash` (resolved via 2-part alias in executeToolWithMeta) |
| Git wildcard | `ns:comp:*` | `system:git:*` (subcommand extracted from `_subcommand` param) |
| Meta tool | `ns:comp:tool` | `system:meta:tools_for_prompt` (captures SkillManager + InferenceProvider) |

## 4. Handler Details

### Core Handlers

| Handler | Input key | Description |
|---------|-----------|-------------|
| `xBash` | `command`, `description`, `[timeout]`, `[workdir]` | Executes shell command. Rejects git and docker commands with error. |
| `xRead` | `file_path`, `[offset]`, `[limit]` | Reads file with line numbers or lists directory entries. Detects binary files. |
| `xGlob` | `pattern`, `[path]` | Recursive file pattern matching. Excludes node_modules, .git, etc. |
| `xGrep` | `pattern`, `[path]`, `[include]` | Regex content search. Excludes binary and large files. |
| `xEdit` | `file_path`, `old_string`, `new_string`, `[replace_all]` | Exact string replacement. Errors on not-found or multiple matches (unless replaceAll). Also accepts camelCase param names (`filePath`, `oldString`, `newString`, `replaceAll`). |
| `xWrite` | `file_path`, `content` | Creates parent directories, writes file. |

### Git Handler

| Handler | Dispatch | Description |
|---------|----------|-------------|
| `xGitCommand` | `system:git:*` via `_subcommand` | Builds `git <subcommand> --flag=value` from params, runs via CommandRunner. Uses `xBuildCommand` helper to convert JSON params to CLI flags with shell escaping. Supports boolean, string, and number flag types. |

### Meta Handlers

| Handler | Dependencies | Description |
|---------|-------------|-------------|
| `xShowSkills` | `SkillManager*` | Navigates skill tree by path (e.g. `/system`, `/local`), lists prompts for a component. |
| `xShowSkillTools` | `SkillManager*` | Lists tools from manifests by component path. |
| `xToolsForPrompt` | `SkillManager*`, `InferenceProvider*` | Builds inventory of all tools/skills, sends to LLM for analysis, validates returned JSON schemas against actual schemas, populates recommendedTools. |

## 5. Bash Git Rejection

```mermaid
sequenceDiagram
    participant LLM
    participant Bash as xBash
    participant Git as xGitCommand / SkillManager

    LLM->>Bash: bash(command: "git status")
    Bash->>Bash: hasTool("git") → true
    Bash-->>LLM: "ERROR: use git_* tools instead"

    Note over LLM,Git: LLM retries via SkillManager dispatch
    LLM->>Git: executeTool("system:git:status", args)
    Git->>CommandRunner: git status
    CommandRunner-->>Git: output
    Git-->>LLM: result
```

## 6. Testing Requirements

| Handler | Test | Input | Expected |
|---------|------|-------|----------|
| `xBash` | Missing command | `{}` | `"ERROR: missing required..."` |
| `xBash` | Echo command | `{"command":"echo hi"}` | `"hi\n"` |
| `xBash` | Git rejection | `{"command":"git status"}` | `"ERROR: git commands must use..."` |
| `xRead` | Missing file_path | `{}` | `"ERROR: missing required..."` |
| `xRead` | File not found | `{"filePath":"/no/such"}` | `"ERROR: file not found"` |
| `xRead` | Directory listing | `{"file_path":"/tmp"}` | Directory entries listed, trailing `/` for subdirs |
| `xRead` | Binary file | `{"file_path":"image.png"}` | `"Type: binary\nSize: ..."` |
| `xRead` | Large file exceeded | `{"file_path":"big.log"}` | First 500 bytes + size info |
| `xRead` | Offset exceeds file | `{"file_path":"f", "offset":9999}` | `"ERROR: offset ... exceeds file length"` |
| `xEdit` | Single replace | Valid file + old/new | `"Edit applied successfully"` |
| `xEdit` | Multiple matches (no replaceAll) | File with 2+ occurrences | `"Error: Found multiple matches..."` |
| `xEdit` | replaceAll=true | File with 2+ occurrences | `"Edit applied successfully"` |
| `xWrite` | New file | Valid path + content | `"Wrote file successfully"` |
| `xGlob` | Glob pattern | Pattern + path | Matching files listed |
| `xGlob` | Trailing `/` pattern | `"dirs/"` | Only directories matched |
| `xGrep` | Pattern match | Pattern + path | Matching lines |
| `xGrep` | Invalid regex | `"[invalid"` | `"ERROR: invalid regex pattern..."` |
| `xGrep` | No matches | Non-existent pattern | `"No matches found for: ..."` |
| `xGitCommand` | Status | Subcommand=status | Git status output |
| `xToolsForPrompt` | Valid analysis | Prompt + SkillManager + provider | `HandlerResult` with output + recommendedTools |
| `xToolsForPrompt` | No provider | Prompt + SkillManager + null | Fallback description, no LLM call |
