**Session ID:** 2026-06-05-opencode-provider-refactoring

**Date / Duration:** June 5, 2026; prompter active ≈ 4.5 hours

**Project / Context:**
Refactoring the a0 C++17 agent's LLM provider architecture from a synchronous `InferenceProvider`/`DeepSeekProvider` design with two divergent execution paths (`AgentCore` for headless `cmdRun`, `DrivenProvider`/`DrivenCore` for TUI `cmdTui`) to a unified async `LlmProvider` interface hierarchy. The `DrivenProvider` base class provides universal `curl_multi` machinery, while `DeepSeekProvider` becomes a thin subclass with provider-specific configuration. Both `cmdRun` and `cmdTui` now share `DrivenCore` with an injected `LlmProvider*`.

**Top-Level Component:**
`LlmProvider` → `DrivenProvider` → `DeepSeekProvider` class hierarchy and unified `DrivenCore` entry point for both CLI modes.

**Second-Level Modules:**
- `src/llm_provider.h` — Pure virtual async interface with `startRequest`/`tick`/`cancel` contract
- `src/driven_provider.h/.cpp` — Refactored to implement `LlmProvider`; `xBuildPayload` + `xAddAuth` extracted as protected pure virtual hooks; `m_apiKey`/`m_model`/`m_baseUrl` moved to `protected`
- `src/deepseek_provider.h/.cpp` — New `DrivenProvider` subclass overriding `xBuildPayload` (OpenAI-compatible JSON) + `xAddAuth` (Bearer token); resolves `DEEPSEEK_API_KEY` env var
- `src/driven_core.h/.cpp` — Changed from `DrivenProvider*` to `LlmProvider*`; added `runSync()` synchronous wrapper; removed stale `tools_for_prompt` call; switched base prompt source from broken `getPrompt("system-base")` to `buildBasePrompt()` from disk
- `src/main.cpp` — Slimmed `AgentStack` (removed `AgentCore`/`SkillRunner`/`ContextManager`/`DepResolver`); rewrote `cmdRun` to use `DrivenCore::runSync()`; `runSkill` replaced with stub error; `cmdTui` now injects `&stack.llmProvider` + `sessionDbId`/`sessionUuid` into `AgentTui`
- `src/tui/agent_tui.h/.cpp` — Constructor changed from `(apiKey, model, ...)` to `(LlmProvider*, ..., sessionDbId, sessionUuid)`; added `m_drivenCore->setSession()` call in constructor and `resumeSession()`; resolved duplicate-session and missing-persistence bugs
- `src/system_handlers.h` — Removed `xToolsForPrompt` declaration and `InferenceProvider` forward decl
- `src/app_core_thread.h/.cpp` — Updated to use new `DeepSeekProvider` subclass
- `CMakeLists.txt` — Removed deprecated sources from build; added new test targets
- `test/unit/test_driven_core_persistence.cpp` — 4 new tests (user message persisted, assistant text persisted, no persistence without session, session switch)
- `test/unit/test_tui_integration.cpp` — 2 new tests (injected session reused, resumeSession wires DrivenCore)
- `*.spec.md` files — Updated 7 specs + added 1 new (`llm_provider.spec.md`) + deprecated 2 (`agent_core`, `skill_runner`)
- `technical-specification.md` (root) — Updated §2 (interface hierarchy), §5 (test tables), §8 (file layout)
- `src/tui/technical-specification.md` — Updated for `LlmProvider*` injection and session params
- `TUI-IMPLEMENTATION-SESSION.md` — Updated for current session additions

**Prompter Contributions:**
- Drove the architectural decision to replace `InferenceProvider` with an async `LlmProvider` interface rather than patching the old synchronous design
- Specified the class hierarchy: `DrivenProvider` as universal base with pure virtual hooks, `DeepSeekProvider` as thin subclass
- Identified that both `cmdRun` and `cmdTui` must share a unified code path via `DrivenCore`
- Caught that `DrivenProvider` should NOT be DeepSeek-specific — the base class must be provider-agnostic
- Rejected implementing `InferenceProvider` compatibility in `DrivenProvider` — correctly identified it as outdated
- Directed that `tools_for_prompt` LLM analysis be removed entirely in favor of static base prompt + tool schema approach
- Specified the `sessionDbId`/`sessionUuid` injection pattern for `AgentTui` to fix duplicate-session and persistence bugs
- Requested TDD approach: write failing tests first, then fix code
- Reviewed test output and identified root causes (mock server legacy path, `xOnComplete` streaming overwrite, `buildBasePrompt` vs `getPrompt("system-base")`)
- Directed deprecation approach: remove from build, keep files on disk as reference
- Requested session evaluation and export at session end

**Model Contributions:**
- Drafted the complete `LlmProvider` abstract interface
- Refactored `DrivenProvider` to implement `LlmProvider`, extracted pure virtual hooks `xBuildPayload` and `xAddAuth`
- Created `DeepSeekProvider` subclass with DeepSeek-specific URL, auth, and payload format
- Updated `DrivenCore` constructor, added `runSync()`, removed `tools_for_prompt` from `xBuildInitialMessages`
- Rewrote `main.cpp` with slim `AgentStack`, unified `cmdRun`/`cmdTui` paths, `runSkill` stub
- Updated `AgentTui` constructor and session wiring, added `resumeSession` fix
- Updated all `.spec.md` files and two `technical-specification.md` files
- Created `test/unit/test_driven_core_persistence.cpp` with 4 tests
- Added 2 session tests to `test_tui_integration.cpp`
- Updated `CMakeLists.txt` build configuration
- Updated `TUI-IMPLEMENTATION-SESSION.md` with current session summary
- Ran full test suites (33 unit + 39 E2E, all passing)

**Prompter Time Estimate:**
- Reading and digesting model responses: ~2.5 hours
- Thinking, strategizing, and weighing options: ~1.0 hours
- Writing messages and directives: ~1.0 hours
- **Total: 4.5 hours** (cumulative, likely over several sittings)

**Model-Equivalent SME Time Estimate:**
- Architecture design and interface definition: 3 hours
- Implementation of `LlmProvider`/`DrivenProvider`/`DeepSeekProvider` hierarchy: 4 hours
- Refactoring `DrivenCore`, `main.cpp`, `AgentTui` for unified path: 4 hours
- Writing and debugging session persistence fix: 2 hours
- Updating CMake build and managing deprecation: 1 hour
- Writing unit tests and E2E debugging: 3 hours
- Updating all spec and technical documentation: 3 hours
- **Total: 20 hours**

**Required SME Expertise:**
- C++17 class hierarchy design with pure virtual interfaces and protected member access
- `curl_multi` asynchronous HTTP transport and event-loop integration
- FTXUI terminal UI framework and event-driven render loop architecture
- SSE streaming response parsing and state machine design
- SQLite-backed session persistence and message sequencing
- CMake build system configuration with FetchContent and conditional compilation
- Software deprecation strategy: removing code from build while preserving reference
- Python E2E testing with PTY-based TUI driver and mock HTTP server
- Technical documentation with Mermaid diagrams and structured spec formats

**Aggregation Tags:**
C++17, LLM provider abstraction, curl_multi, async tick-based API, DrivenCore, DeepSeek API, FTXUI TUI, session persistence, SQLite, SSE streaming, ResponseDecoder, DependencyGraph, technical specification, spec.md, test-driven development, GTest, Python E2E, architectural refactoring, code deprecation
