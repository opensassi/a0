## Agent Prompt: CLI Skill Scaffolder — System Tier

Continue from Base phase 2. Tier: **system**.

### Phase 3 — Build the manifest

Determine `name`: use `{{name}}` if provided, otherwise `{{program}}`.

Create `skills/system/<name>/skill.json` with:

- `"name": "<name>"`
- `"version": "1.0.0"`
- `"description"`: from `{{program}} --help` first line
- `"tools"`: exhaustive array — every command from help output gets a tool entry
- `"prompts"`: empty array `[]`

Each tool entry follows this structure:

```json
{
  "name": "<subcommand>",
  "description": "<one-line from --help>",
  "systemTool": true,
  "parameters": {
    "type": "object",
    "properties": {
      "<param>": { "type": "string|boolean|number", "description": "..." },
      "args": {
        "type": "array",
        "items": { "type": "string" },
        "description": "Additional arguments passed to <program> <subcommand>"
      }
    },
    "required": []
  }
}
```

**Auto-detect flags**: For each command, run `{{program}} <cmd> --help` (or detected help convention). Parse flag lines:
- `--flag` with no argument → `boolean`
- `--flag <arg>` → `string`
- `--flag <n>` → `number`

Select the 2-5 most user-facing flags per command for typed params. Internal/developer flags go to `args` catch-all. If no flags can be determined, the tool gets only the `args` parameter.

### Phase 4a — Generate regeneration script

Create `scripts/gen-<name>-tool-defs.sh` that is:

1. **Executable**: starts with `#!/usr/bin/env bash` and `set -euo pipefail`
2. **Idempotent**: reads existing `skills/system/<name>/skill.json`, preserves `name`/`version`/`description`/`prompts`, regenerates only the `tools` array
3. **Self-contained**: uses Python embedded via heredoc (`python3 << 'PYEOF'`)
4. **Auto-detecting**: runs `{{program}} --help` and per-command `--help` to enumerate commands and parse flags, using the same auto-detection logic as phase 3
5. **Writes back**: outputs the updated manifest to the same path

It should accept `SKILL_JSON` environment variable override (default: `skills/system/<name>/skill.json`).

### Phase 4b — C++ changes

#### 4b.1 Handler group registration

In `src/system_tools/registry.cpp` constructor, add after existing handler groups:

```cpp
m_handlerGroups["<name>"] = [](const std::string& tool, const json& p) {
    return x<Name>Command(tool, p);
};
```

#### 4b.2 Handler group declaration

In `src/system_tools/registry.h`, add:

```cpp
static SystemToolResult x<Name>Command(const std::string& subcommand, const json& params);
```

#### 4b.3 Handler group implementation

Create `src/system_tools/<name>_handlers.cpp`:

```cpp
#include "registry.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

SystemToolResult SystemToolRegistry::x<Name>Command(
    const std::string& subcommand, const json& params) {
    std::string cmd = buildCommand("<program> " + subcommand, params);
    return runCliCommand(cmd, params, 60);
}
```

Reuse the `buildCommand()` and `runCliCommand()` static helpers from `git_handlers.cpp` or `docker_handlers.cpp`.

#### 4b.4 Prefix routing

In `src/system_tools/registry.cpp` `execute()`, add before the direct lookup fallback:

```cpp
if (name.rfind("<name>_", 0) == 0) {
    auto it = m_handlerGroups.find("<name>");
    if (it != m_handlerGroups.end())
        return it->second(name.substr(<prefix_len>), params);
}
```

`<prefix_len>` is the length of `"<name>_"` string.

#### 4b.5 Bash rejection (optional)

In `src/system_tools/core_handlers.cpp` `xBash()`, add before the command runs:

```cpp
if (hasTool("<program>")) {
    return {"ERROR: <program> commands must use the <name> system tools. "
            "Browse tools: show_skill_tools('/<name>')."};
}
```

This is recommended for security-sensitive tools (git, docker, etc.) that interact with the agent's security boundary.

#### 4b.6 CMake

In `src/system_tools/CMakeLists.txt`, add `<name>_handlers.cpp` to the source list.

### Phase 5 — Build and test

1. Run the generation script: `bash scripts/gen-<name>-tool-defs.sh`
2. Rebuild: `cd build && cmake .. && make -j$(nproc)`
3. Run the test suite: `ctest`
4. Quick smoke test: `echo '{"goal":"<program> --help"}' | ./build/a0 --run` — should trigger `tools_for_prompt` analysis and discover the new tools
