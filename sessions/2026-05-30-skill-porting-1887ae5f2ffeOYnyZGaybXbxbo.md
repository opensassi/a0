**Session ID:** 2026-05-30-skill-porting

**Date / Duration:** 2026-05-30; prompter active ≈ 6 hours

**Project / Context:**
Port the system-design and session-evaluation skills from the `@opensassi/opencode` npm package (opencode harness format) into the a0 C++17 agent's native skill system. This involved a fundamental architectural migration: replacing the legacy `FileSystemSkillRegistry` with a new `SkillManager`/`SkillLoader` layer, adding composite skill support (chain-based prompt composition), creating a dispatch table with collision-resolving LLM-facing name generation, implementing template substitution (`{{tool:...}}` and `{{tool_call:...}}`), and building a comprehensive synthetic test framework with 19 parameterized test cases.

**Top-Level Component:**
a0 skill system infrastructure — `SkillManager`, `SkillLoader`, `DefaultSkillRunner`, `DefaultAgentCore` — with two ported skills (system-design: 18 commands, session-evaluation: 2 commands).

**Second-Level Modules:**
- `skills.h` / `skill_loader.cpp` — Added `systemTool`, `chain`, `promptFile`, `timeoutSecs` fields; merged `SkillPrompt` → `Prompt` (one unified type)
- `skill_manager.cpp` — `getPromptResolved()` with recursive chain flattening, `resolveName()` with component-scoped lookup, `buildDispatchTable()` with collision resolution
- `skill_runner.cpp` — Regex update for `[\w:-]+` qualified names, global var substitution (`{{SESSION_ID}}`, `{{PROJECT_DIR}}`), short-name fallback via `ns:component:name`
- `agent_core.cpp` — Replaced `SkillRegistry*` with `SkillManager*`, added `DispatchTable` with runtime type detection, wired `SESSION_ID`/`LOGS_DIR`/`PROJECT_DIR` global vars
- `dependency_resolver.cpp` / `base_prompt.cpp` — Switched from `SkillRegistry*` to `SkillManager*`
- `CMakeLists.txt` — Added skills sub-module .cpp files, removed `skill_registry.cpp`
- `main.cpp` — Instantiate `SkillManager` instead of `FileSystemSkillRegistry`, added `--export-session` flag
- `invocation_logger.h/cpp` — `exportSession()` method for JSONL-to-JSON array export
- `skills/system/` — 8 components migrated from flat `.tool.json` to `skill.json` manifests
- `skills/local/opensassi/system-design/` — Full port: 19 prompt files + 5 validation scripts, `{{tool_call:...}}` syntax adapted from opencode MCP refs
- `skills/local/opensassi/session-evaluation/` — Full port: 2 command prompts + `export_session.sh` tool, `{{SESSION_ID}}` integration
- `test/fixtures/` — 13 fixture directories with synthetic manifests, `skill_resolver_tests.json` (19 test cases)
- `test/unit/test_skill_resolver.cpp` — Parameterized test runner with 11 step action types, real `SubprocessToolRunner` and `SystemToolRegistry`

**Prompter Contributions:**
Directed the overall architecture: two-type merge (`Prompt`/`SkillPrompt`), automatic short-name resolution via component context, collision-resolving dispatch table as a `SkillManager` method rather than an `agent_core` stub. Made key design decisions: `chain` composition vs `basePrompt` field, `inputMode: "args"` with `--key=value` interface for tools, `scripts/` → `bin/` naming, and the deferred items list for future sessions. Identified and corrected the regex character class bug (`[\w:]+` → `[\w:-]+` for hyphens in qualified names). Pushed for real (unmocked) tests using `SubprocessToolRunner` and `SystemToolRegistry`.

**Model Contributions:**
Implemented all C++ changes across 15+ files: `skills.h`, `skill_loader.cpp`, `skill_manager.cpp`, `skill_runner.cpp`, `agent_core.h/cpp`, `dependency_resolver.h/cpp`, `base_prompt.h/cpp`, `main.cpp`, `agent_interfaces.h`, `invocation_logger.h/cpp`, `CMakeLists.txt`. Wrote 19 prompt files + 5 bash scripts for the two ported skills. Built the synthetic test framework: 13 fixture manifests, 19-parameter test descriptor, parameterized C++ test runner with `SkillRunner`, `ToolRunner`, and `SystemToolRegistry` wired in. Fixed the `fs::path` trailing-slash bug in test sandboxing. Reduced full suite runtime from ~90s to 3s by making `timeoutSecs` configurable per-tool. Added `--export-session` CLI flag, global template variables, and `exportSession()` to `InvocationLogger`.

**Prompter Time Estimate:**
- Reading and digesting model responses: ~3.0 hours
- Thinking, strategizing, and weighing options: ~1.5 hours
- Writing messages and directives: ~1.5 hours
- **Total: 6.0 hours**

**Model-Equivalent SME Time Estimate:**
~40 hours. Breakdown: C++17 system architecture design (6h), SkillManager/SkillLoader implementation (8h), agent_core/processGoal rewrite (6h), test framework + 19 test cases (8h), skill file authoring — 19 prompts + 5 scripts (8h), debugging + fixing (4h).

**Required SME Expertise:**
- C++17 class design and interface contracts
- CMake build system configuration and static library linking
- JSON Schema design for nested skill manifests
- Shell scripting (bash, bzip2, sha256sum, pipe handling)
- Test-driven development with Google Test parameterized suites
- Subprocess management (fork/exec/waitpid/alarm)
- Docker CLI integration patterns
- Recursive dependency resolution with cycle detection
- Regex-based template engine design
- Linux filesystem operations (inode iteration, recursive copy, symlinks)

**Aggregation Tags:**
c++, skill-system, cmake, test-framework, prompt-engineering, bash, docker, gtest, template-engine, session-evaluation
