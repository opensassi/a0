## Agent Prompt: CLI Skill Scaffolder — Base

### Input

A CLI program name (the `program` parameter), and optionally `name` to override the skill component name if it differs from the binary name.

### Phase 1 — Locate and interrogate the program

1. Run `which {{program}}` to confirm it exists. If not found, search common install paths (`/usr/bin`, `/usr/local/bin`, `$HOME/.local/bin`, `$HOME/.nvm/versions/node/*/bin`) or report error.
2. Run `{{program}} --help` (or `--help-all` / `help` depending on convention) to enumerate all subcommands and their descriptions.
3. Determine the help convention by checking: does it use `--help`, `help`, `-h`? Do subcommands accept `--help`? Run `{{program}} <subcommand> --help` on a sample subcommand to confirm.
4. For programs with management-command groups (e.g., `container`, `image`, `network`), recursively discover sub-subcommands via `{{program}} <group> --help`.
5. Validate all discovered tool names against `^[a-zA-Z0-9_-]+$`. Skip or sanitize entries with invalid characters.

### Phase 2 — Determine target tier

The tier is known from which prompt was called:
- `create_local_cli_skill` → tier = `local`, `systemTool = false`
- `create_system_cli_skill` → tier = `system`, `systemTool = true`

### Output format

When scaffolding is complete, report:

```
Scaffolded skill for <program>:
  Tier: <local|system>
  Tools: <N> commands registered
  Manifest: skills/<tier>/<name>/skill.json
  Generation script: scripts/gen-<name>-tool-defs.sh
  <if system> Handler group: <name> → x<Name>Command
  <if system> Bash rejection: <enabled|skipped>
```

### Design principles

- **Exhaustive tool creation**: Every command from the CLI help output gets a tool entry. No curation at scaffold time.
- **No hardcoded high-level prompts**: The `prompts` array is left empty. Multi-step workflows are generated on-the-fly by the LLM via `tools_for_prompt` enumeration.
- **`args` catch-all**: All tools get an `args: string[]` parameter for CLI flags not covered by structured params. Only the most common 2-5 flags per command get typed params.
- **`systemTool` must be set**: `true` for system tier, `false` for local tier.
- **Handler group naming**: The group name matches the skill component name (e.g., `kubectl` → `m_handlerGroups["kubectl"]`, `cargo` → `m_handlerGroups["cargo"]`).
- **`local` skills need no C++**: They're resolved by `SkillManager` from the manifest. `{{tool:<name>_<subcommand> ...}}` expansion works through `getTool()` → `ToolRunner::run()`.
