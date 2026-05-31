**Session ID:** 2026-05-30-git-docker-system-tools
**Date / Duration:** 2026-05-30; prompter active ≈ 5.5 hours

**Project / Context:**
Refactoring and extending the a0 C++17 agent's system tool architecture — converting hardcoded compiled-in tool handling into a skill-based discovery system. Adding git, docker, and docker-compose as first-class system skills with skill manifests, template-based `{{tool:...}}` expansion, bash git/docker rejection, and a two-phase forked execution loop with automatic `tools_for_prompt` analysis.

**Top-Level Component:**
Git system skill + skill-based tool infrastructure (handler groups, skill manifests, template expansion, discovery)

**Second-Level Modules:**
- 163 git commands as skill tool entries in `skills/system/git/skill.json` with structured params
- 64 docker commands in `skills/system/docker/skill.json` + 35 docker-compose commands
- Filesystem tools consolidated into `skills/system/fs/` (5 tools, 5 skill prompts removed)
- `system_tools.cpp` → `src/system_tools/` sub-module (registry, core, git, discovery, docker handlers)
- `DockerSecurityFilter` for sandbox container protection
- Two-phase forked execution in `agent_core.cpp` with auto `tools_for_prompt` injection
- Base prompt stripped to minimal identity (build hash + OS + CWD)
- Snake_case parameter migration (`file_path`, `old_string`, `new_string`)
- Bash git/docker/compose command detection and rejection
- Generation scripts for tool definitions (`gen-git-tool-defs.sh`, `gen-docker-tool-defs.sh`)

**Prompter Contributions:**
- Designed the skill-based architecture (handler groups, `system_` prefix routing, `isSystemTool` refactoring)
- Specified the two-phase forked execution model with context rewind
- Directed auto `tools_for_prompt` injection on every turn
- Defined the naming convention (`system_git_status`, `system_fs_read`, snake_case params)
- Designed the `DockerSecurityFilter` for sandbox container isolation
- Corrected course on API naming constraints, exhaustive tool listing, and prompt minimality

**Model Contributions:**
- Implemented all C++ code: registry, handler groups, prefix routing, execute dispatch
- Created all skill manifests and prompt templates
- Wrote generation scripts for git/docker/docker-compose tool definitions
- Refactored `system_tools.cpp` into sub-module with 6 source files
- Updated all test fixtures for renamed skill paths
- Implemented bash git/docker/compose rejection
- Wired `DockerSecurityFilter`, `setInferenceProvider`, `setSkillManager` in main.cpp
- Updated `base_prompt.cpp` to identity-only, `schemas()` to 5-tool minimum

**Prompter Time Estimate:**
- Reading and digesting model responses: ~3.0 hours
- Thinking, strategizing, and weighing options: ~1.5 hours
- Writing messages and directives: ~1.0 hours
- **Total: ~5.5 hours**

**Model-Equivalent SME Time Estimate:**
~40 hours of senior C++ engineer time, broken down:
- Architecture design and spec: 6 hours
- Git skill implementation (163 commands, handler groups, templates): 8 hours
- Docker/compose skill implementation (99 commands, security filter): 6 hours
- System sub-module refactoring: 4 hours
- Test fixture updates and debugging: 6 hours
- Discovery/analysis infrastructure (tools_for_prompt, show_skills): 4 hours
- Generation scripts and tool data: 3 hours
- Prompt engineering and iteration: 3 hours

**Required SME Expertise:**
- C++17 systems programming with nlohmann/json
- CMake static library sub-module architecture
- LLM function-calling API schema design (`^[a-zA-Z0-9_-]+$` naming constraints)
- Git CLI command interface and parameter schemas
- Docker/Docker Compose CLI command interface
- JSON Schema design for tool parameters
- Google Test framework for C++ unit testing
- Bash scripting for tool definition generators
- Linux subprocess management (fork/exec/pipe/alarm)

**Aggregation Tags:**
git, docker, docker-compose, system-tools, cpp17, skill-system, security-filter, function-calling, tool-discovery, agent-architecture, cmake, sub-module, test-refactoring
