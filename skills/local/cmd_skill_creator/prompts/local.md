## Agent Prompt: CLI Skill Scaffolder — Local Tier

Continue from Base phase 2. Tier: **local**.

### Phase 3 — Build the manifest

Determine `name`: use `{{name}}` if provided, otherwise `{{program}}`.

Create `skills/local/<name>/skill.json` with:

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
  "systemTool": false,
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

### Phase 4 — Generate regeneration script

Create `scripts/gen-<name>-tool-defs.sh` that is:

1. **Executable**: starts with `#!/usr/bin/env bash` and `set -euo pipefail`
2. **Idempotent**: reads existing `skills/local/<name>/skill.json`, preserves `name`/`version`/`description`/`prompts`, regenerates only the `tools` array
3. **Self-contained**: uses Python embedded via heredoc (`python3 << 'PYEOF'`)
4. **Auto-detecting**: runs `{{program}} --help` and per-command `--help` to enumerate commands and parse flags, using the same auto-detection logic as phase 3
5. **Writes back**: outputs the updated manifest to the same path

It should accept `SKILL_JSON` environment variable override (default: `skills/local/<name>/skill.json`).

### Phase 5 — Build and test

1. Run the generation script: `bash scripts/gen-<name>-tool-defs.sh`
2. Verify the manifest with a quick check: `python3 -c "import json; d=json.load(open('skills/local/<name>/skill.json')); print(len(d['tools']), 'tools')"`
3. Confirm the manifest is valid JSON and all tool names pass `^[a-zA-Z0-9_-]+$`
