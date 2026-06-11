# Project Summary

Over 44 AI-assisted development sessions spanning 289.7 hours (12.1 calendar days) from May 28 through June 9, 2026, the project built `a0` — a self-evolving C++17 agent that connects to the DeepSeek LLM API, executes tools via subprocess with Docker isolation, manages a filesystem-based skill repository, and provides both a headless CLI and a full FTXUI terminal interface backed by a three-process supervision architecture (a0 agent, b1 supervisor daemon, c2 HTTP dashboard). A total of 11,590 messages were exchanged across 8,517 build-mode and 1,914 plan-mode invocations, consuming 2,651,207,579 tokens at $14.73 total API cost with a 98.5% cache hit rate. The prompter contributed 15.7684 hours of gap-capped active engagement; session self-assessments estimated 135.4883 hours of prompter time and 1,243.5–1,497.5 hours of senior engineer–equivalent effort. Seventeen distinct tools were built, with bash (3,317 calls), read (3,049), edit (2,950), write (744), and grep (728) accounting for the majority of the 11,732 tool-call parts. The system underwent continuous architectural evolution: from a monolithic build to 12 static libraries, from synchronous libcurl to async `curl_multi`, from a single-threaded TUI to a two-thread MPSC-channel architecture, from JSONL files to SQLite persistence, and from compiled-in tool handlers to a unified `SkillManager` dispatch layer with JSON Schema validation.

## Productivity Overview

| Metric | Value | Source |
|--------|-------|--------|
| Calendar span | 289.7 hours (12.1 days) | `actual-times-summary.json` / calendar_span_hours |
| Sessions | 44 | `time-estimates-summary.json` / session_count |
| Total messages | 11,590 | `actual-times-summary.json` / total_messages |
| Total tokens billed | 2,651,207,579 | `actual-times-summary.json` / total_tokens_billed |
| Total API cost | $14.726041 | `actual-times-summary.json` / total_cost |
| Cache hit rate | 98.5% | `actual-times-summary.json` / avg_cache_hit_pct |
| Prompter active (gap-capped) | 15.7684 hours total, 0.36 hours avg per session | `actual-times-summary.json` / total_prompter_active_hours |
| Prompter estimated | 135.4883 hours | `time-estimates-summary.json` / total_prompter_hours_min |
| SME-equivalent estimated | 1,243.5 – 1,497.5 hours | `time-estimates-summary.json` / total_sme_hours_min / total_sme_hours_max |
| Mode usage | 8,517 build, 1,914 plan | `actual-times-summary.json` / mode_totals |
| Finish reasons | 9,286 tool-calls, 992 stop | `actual-times-summary.json` / finish_reason_totals |

**Most-used tools across all sessions:** bash (3,317), read (3,049), edit (2,950), write (744), grep (728), todowrite (503), glob (210), task (101), question (44), skill (33), webfetch (20), playwright_browser_navigate (12).

**Part type totals:** tool (11,732), step-start (10,414), step-finish (10,278), reasoning (9,709), text (3,999), patch (3,223).

**Methodology note — engagement metrics.** The prompter active time of 15.7684 hours is measured via a gap-capping method: for each user message, the time elapsed since the preceding model output is summed, capped at 60 seconds per gap to exclude context-switching pauses. This produces a conservative lower bound — long thinking or reading periods are truncated at 60 seconds regardless of actual duration. The 135.4883-hour estimated prompter figure is a self-reported assessment from session evaluations and is not directly comparable to the gap-capped measurement. More accurate engagement metrics would require input-event instrumentation (keystroke tracking, focus detection, scroll and click monitoring) rather than relying solely on message timestamps. The 1,243.5–1,497.5-hour SME-equivalent estimate reflects the session author's assessment of how long a senior engineer would need to produce equivalent output.

### Session ID
`opensassi/a0`

### Date / Duration

The project spanned 289.7 hours (12.1 calendar days) across 44 sessions from May 28 through June 9, 2026. What began as a single-session MVP implementation rapidly expanded into a sustained development effort across four distinct phases. The first two days (May 28–29) established the foundational architecture: a C++17 agent binary with DeepSeek API connectivity, a Docker containerization sub-module, and a specification revision cycle that tightened subprocess lifecycle management. Days three through five (May 30–31) saw explosive growth as the system tool layer, the b1/c2 supervision hierarchy, the skill ecosystem, streaming terminal infrastructure, and SQLite persistence were built out in parallel across overlapping sessions. The middle weekend (June 1) brought an enterprise planning and stakeholder review interlude that produced a formal product roadmap, 81 GitHub issues, and audit reconciliation documents. Days six through eight (June 2–4) were dominated by architectural refactoring — the `SystemToolRegistry` elimination, the `SessionContext`/worktree feature, `SkillManager` unified dispatch, and the initial FTXUI-based TUI sub-module implementation. Days nine through eleven (June 5–6) focused on the threading architecture overhaul that decoupled the TUI from the application core via MPSC channels, the async `LlmProvider` abstraction, persona system, and the concurrency model specification. The final two days (June 8–9) concentrated on TUI polish (collapsible tool calls, scrolling, word wrapping), a bottom-up specification tree revision spanning 61 spec files, the persistence-first I/O architecture, a sub-module design review, and a large-scale source tree reorganization that decomposed the monolithic `a0_lib` into six independent library targets.

### Project / Context

The project built `a0` — a minimal self-evolving C++17 agent that connects to the DeepSeek (OpenAI-compatible) LLM API, maintains a filesystem-based repository of tools and skills, executes subprocess tools with Docker isolation, and provides both a headless CLI mode and a full FTXUI-based terminal user interface. The architecture follows a three-process hierarchy: `a0` (per-project agent binary with REPL or TUI interface), `b1` (per-workdir supervisor daemon managing a0 lifecycle and IPC routing), and `c2` (machine-level monitor with an HTTP dashboard and SSE push). The system was designed from day one with testability as a first-class concern — virtual interfaces for dependency injection, Google Test unit tests with mock infrastructure, Python PTY-based E2E tests, and a Python mock DeepSeek server. The project underwent continuous architectural evolution: from a monolithic build target to 12 static libraries, from synchronous libcurl calls to async `curl_multi`, from a single-threaded TUI embedding the agent core to a two-thread MPSC-channel architecture with emphatic boundary enforcement between UI and application logic. Event-driven design, thread-safe message passing, specification-first development, and persistent storage for deterministic replay were recurring themes throughout.

### Top-Level Components

**Agent Core (`agent_core` → `driven_core`):** The execution loop evolved through three major architectures — an initial REPL with `SkillRunner::execute` (text-only LLM completions), a forked tool-calling loop (`xRunForkedLoop` with `processGoalStreaming`), and finally the `DrivenCore` three-state state machine (Idle → AwaitingLlm → ExecutingTools) shared across both CLI and TUI modes, driven by an async `LlmProvider` tick interface.

**LLM Provider Layer (`InferenceProvider` → `DrivenProvider` → `LlmProvider`):** Starting with a synchronous libcurl-based `DeepSeekProvider`, the architecture was refactored into a polymorphic `LlmProvider` abstract interface with `DrivenProvider` providing universal `curl_multi` async machinery and `DeepSeekProvider` as a thin configuration subclass. The `ResponseDecoder` handles SSE/JSON auto-detection and streaming token accumulation.

**System Tools & Skill System:** Grew from six compiled-in C++ handlers (bash, read, glob, grep, edit, write) through a `SystemToolRegistry` class, then merged into `SkillManager` as a unified dispatch layer. The skill system evolved from a flat `ComponentRegistry` through a three-tier namespace architecture (system/local/github_USER) with manifest-based `skill.json` files, `DependencyGraph`-based parallel execution, `ToolState` for cross-invocation state, and JSON Schema validation via valijson. The git skill alone contained 163 command definitions.

**Docker Sub-Module:** Containerized tool execution with trust-level-based container pooling (HIGH/MEDIUM/LOW tiers), idempotent apt dependency installation, Docker Compose environment orchestration, `DockerSecurityFilter` for sandbox isolation, and session-aware container naming.

**b1 Supervisor Daemon:** Per-workdir process supervisor registering a0 instances, detecting crashes via `waitpid`, routing IPC messages between a0 processes and the c2 dashboard, and forwarding streaming terminal events without owning the subprocess lifecycle.

**c2 Monitor Dashboard:** HTTP/SSE server built on uWebSockets with dual-thread design, WebComponents-based SPA (15+ components across 5 pages), REST API for multi-host agent management, user prompt routing, terminal launcher with xterm.js integration, and SSE reliability model with client ping/pong.

**SQLite Persistence Layer:** Abstract `PersistenceStore` interface with `SqliteStore` implementation using WAL mode. Schema evolved from 3 tables to support sub-session fork tracking, streaming chunks, task tree management, session metadata, and resource storage. `ReplayEngine` provides deterministic message-driven replay.

**TUI Sub-Module:** FTXUI-based terminal interface with 18 source files including message panel (scrolling, word wrapping, collapsible tool calls), markdown renderer (MD4C SAX parser), input panel (history ring buffer, bracketed paste), status bar (thread-safe flash messages), session manager, dialog system, and clipboard support (OSC 52 + xclip fallback).

**IPC & Concurrency:** Unix domain socket protocol with JSON-line framing, later augmented with BufferedSocket for efficient reads. A cross-cutting concurrency model specification documented 9 concurrency contexts across all three processes with C4 diagrams. MPSC (multi-producer single-consumer) channels with eventfd wakeup provide thread-safe communication between the AppCoreThread and TUI thread.

**Persona System:** Directory-based persona definitions with `persona.json` manifests and prompt files, `PersonaLoader` that walks system/local/github namespaces, and persona-based tool filtering in `DrivenCore::xBuildToolSchemas()`.

### Second-Level Modules

**Foundation & Build:** CMake build system (FetchContent dependency management, 12+ static library targets, `ENABLE_TRACE` compile definitions, coverage instrumentation with lcov/gcov), CLI11 argument parsing (subcommand-based CLI with session export/list/run/terminal/kill-all), .env file loader, TRACE_LOG instrumentation framework, hex session ID generation (CSPRNG via /dev/urandom).

**Persistence & Session Management:** `SessionContext` with git worktree creation/resume, `InvocationLogger` removal and consolidation into `SqliteStore`, sub-session fork model with `UNIQUE(session_id, sub_session_id, seq)` primary keys, session export pipeline (JSONL with gzip compression, prefix-based UUID lookup), `NullResourceProvider`/`SqliteResourceProvider` for stream-based resource storage.

**Testing Infrastructure:** 32+ unit test suites (Google Test), 13+ agent E2E tests (bash orchestrated with mock DeepSeek server), 18+ TUI E2E tests (Python PTY-based with `TuiDriver` class, `MockServer` with scenario JSON fixtures), c2 dashboard E2E (Playwright with `selectById` bridge action), fixture generation pipeline (`session-to-fixture.py` converting real exports to deterministic scenarios), `scripts/cleanup-dev.sh` for zombie process cleanup.

**Developer Tooling & Documentation:** Skill developer guide (skills/README.md, 661 lines), persona developer guide (personas/README.md), TUI design and engineering docs (tui-design.md, tui-eng.md), AGENTS.md with 8-rule debugging protocol, concurrency model specification with 7-expert panel review, bottom-up specification tree (61 .spec.md files + 13 technical-specification.md files).

**Integration & External Systems:** Docker CLI wrapper with fork/exec/alarm-based timeouts, Docker Compose orchestration with per-skill network tracking, Playwright browser automation (Node.js daemon with 22 browser actions), Cap'n Proto IPC schema for future migration, external repo clone support (`--external-repo` flag), opensassi skill ecosystem with 13 skills and 60+ commands.

### How the Project Started

The project began on May 28 with a single ambitious session that defined a complete MVP specification — all nine modules of a minimal self-evolving C++17 agent. The prompter chose virtual interfaces over concrete classes for dependency injection and testability, selected nlohmann/json and Google Test as library dependencies, and directed a TDD workflow (stubs → tests → implementation). The first session produced 83 unit tests across 10 test suites, 4 E2E integration tests, a Python mock DeepSeek server, and comprehensive line coverage. The initial architecture was uncompromisingly rigorous: abstract interfaces from the start, comprehensive test coverage, and a clear separation between the agent core, tool execution, skill orchestration, and LLM communication. The earliest architectural decisions — virtual interfaces for DI, `popen`-based subprocess management, JSON Lines session logging, and file-based component registries — established patterns that would be revisited, refactored, and replaced over the coming days but whose architectural intent (testability, observability, modularity) persisted throughout the project.

### What Was Developed

The first day delivered the MVP but also immediately revealed the need for containerized execution, so a focused Docker sub-module session followed hours later with trust-level pooling, Compose orchestration, and thorough test coverage. The same evening, v2 spec revisions tightened subprocess lifecycle management with `fork/exec/waitpid` + `SIGALRM` timeout enforcement and recursive dependency resolution.

Day two (May 29) was a turning point — the b1/c2 supervision architecture was designed and implemented, the IPC library with Unix domain sockets was built, and the foundational rename from `ComponentRegistry`→`SkillRegistry` and `components/`→`skills/` was performed. Simultaneously, the system tool infrastructure with six C++ native handlers and agentic function-calling was implemented, and the skills/persistence/command-runner sub-modules were redesigned with a centralized `CommandRunner` consolidating all subprocess management.

Day three (May 30) saw parallel explosions across multiple fronts: the c2 web UI with SSE and WebComponents SPA was built, coverage was raised through extensive new test cases, numerous git/docker/docker-compose commands were defined as skill manifests, the system-design and session-evaluation skills were ported from the npm package, and the streaming terminal infrastructure across all five sub-modules was implemented.

Day four (May 31) concentrated on architecture: the agent core was restructured so skill prompts execute inside the tool-calling forked loop, dynamic tool accumulation replaced static filtering, session IDs were migrated from timestamp-based to hex format, the entire specification tree was audited and revised, and the persistence layer was overhauled from JSONL to SQLite with sub-session tracking and CLI11 integration.

June 1 was an enterprise planning interlude. Two comprehensive review panels (Enterprise Stakeholder and System Design) were convened, producing codebase audit findings, an extensive product roadmap, competitive analysis across multiple tools, a zero-trust security model, and handoff documentation for parallel agent-driven development.

Days June 2–3 focused on architectural cleanup and consolidation: systemic separator mismatches were fixed across all call sites, the `SessionContext`/worktree feature was implemented, `SystemToolRegistry` was eliminated by merging into `SkillManager`, stale spec files were revised, and the unified dispatch was completed. The c2 dashboard gained an agent interaction page with Playwright E2E testing, the terminal launch flow was fixed through b1, and the skill system saw `DependencyGraph`-based parallel execution, `ToolState`, the Playwright browser automation skill, and JSON Schema validation.

Days June 4–5 were dominated by the TUI sub-module. The initial FTXUI implementation replaced the stdin REPL as the default mode. SSE-based token streaming was wired through the TUI with multi-turn tool-calling. A comprehensive Python PTY-based E2E harness was built. The `LlmProvider`/`DrivenProvider`/`DeepSeekProvider` class hierarchy replaced the old synchronous design. The `DrivenCore` three-state state machine unified CLI and TUI execution paths. The threaded App Core architecture (Phase B) was implemented with MPSC channels, `curl_multi` async provider, and `ResponseDecoder`. The TUI threading model was corrected to the C2 two-thread design with emphatic boundary enforcement — the TUI became a thin rendering client with zero core references. Streaming multi-turn bugs were fixed across the `ResponseDecoder`, `DrivenCore`, and mock server. The BufferedSocket IPC refactor replaced one-byte-at-a-time reads, and the concurrency model specification with expert panel review was produced.

June 6 refined the TUI (word wrapping, auto-scroll) and implemented the persona system with tool filtering, task manager skill, and the qualified name convention rename. A persona filtering bug was identified and fixed where the empty default `--persona` flag caused all tools to load instead of the defined subset.

June 8 delivered TUI polish: collapsible tool call blocks with click-to-toggle via `ftxui::reflect`, scroll fixes (PgUp/PgDown/Home/End/mouse wheel), virtual window elimination, tool argument display, and the `RoundComplete` MPSC event.

June 9, the final day, was a comprehensive wrap-up: a bottom-up specification tree update (many new specs, updates, regenerated technical specs), the persistence-first I/O architecture with `ResourceProvider` abstraction and new MPSC event types, a sub-module design review producing two detailed implementation plans, and the large-scale source tree reorganization that decomposed `a0_lib` into six independent library targets with extensive file relocations and dead code removal.

### Subject Matter Expertise Required

**C++ Development (deep, exercised daily):** Modern C++17/20 with STL containers, smart pointers, `std::variant`, `std::atomic`, `std::filesystem`, lambda expressions, RAII, virtual interfaces, template metaprogramming. Class hierarchy design with pure virtual interfaces, protected member hooks, and dependency injection. State machine design (three-state `DrivenCore`). Thread-safe MPSC channel implementation with eventfd. Signal handling with `sigaction`, `SIGALRM`, `volatile sig_atomic_t`, `std::atomic<int>` with signal fences.

**POSIX Systems Programming (deep):** Subprocess lifecycle management (`fork`, `exec`, `waitpid`, `pipe`, `dup2`, `setpgid`, `setsid`). PTY allocation and terminal programming. Unix domain sockets and poll/epoll event loops. Process group and session management. Daemonization patterns. `/proc/self/exe` path resolution. PID file management and kill-by-process-name.

**Build Systems & Tooling (deep):** CMake multi-target build architecture (12+ static libraries, FetchContent, transitive dependencies, `target_compile_definitions`, `RUNTIME_OUTPUT_DIRECTORY`). Coverage instrumentation with gcov/lcov/genhtml (branch coverage, HTML reports). CLI11 argument parsing library. Git worktree-based development workflows and rebase-based atomic commits.

**LLM Integration (deep):** DeepSeek/OpenAI function-calling API (tool schemas, `tool_call` response parsing, assistant message insertion). SSE streaming protocol (`data:` lines, delta events, `finish_reason` accumulation). `curl_multi` async HTTP with non-blocking DNS resolution. `libcurl` options (`CURLOPT_WRITEFUNCTION`, `CURLOPT_POSTFIELDS`, `CURLOPT_POSTFIELDSIZE`). Prompt engineering for structured JSON output (dynamic tool accumulation, `tools_for_prompt` analysis).

**Database Engineering (intermediate-to-deep):** SQLite schema design and C API (`sqlite3_prepare_v2`, `sqlite3_bind_*`, `sqlite3_step`). WAL mode concurrency for single-writer multi-reader. Schema migration patterns. UNIQUE constraint semantics with NULL values. Sub-session fork tracking with compound primary keys.

**Testing Infrastructure (deep):** Google Test framework (fixtures, assertions, matchers, parameterized tests, `EXPECT_DEATH`). Python PTY-based E2E test harness (`pty.openpty`, `os.fork`, `os.execve`, `select.poll`). Playwright headed-browser automation with shadow-DOM bridge actions. Python mock HTTP server design (stateful multi-turn scenarios, SSE mode). Fixture generation from real session exports.

**Terminal UI (deep):** FTXUI v6 component system (`Container::Vertical`, `Renderer`, `CatchEvent`, `Modal`, `Element`, `ScreenInteractive`, `focus`/`select` decorators, `paragraph`, `hflow`, `reflect` boxes). MD4C SAX-style markdown parser integration. ANSI escape sequences, SGR mouse tracking, bracketed paste mode (DEC 2006), OSC 52 clipboard protocol. xterm.js web terminal integration.

**Web Development (intermediate):** uWebSockets HTTP/SSE server with TLS. WebComponents v1 (Custom Elements, Shadow DOM, `observedAttributes`). ES module architecture and browser caching. SPA client-side routing with History API.

**Networking & IPC (intermediate):** Unix domain socket protocol design with JSON-line framing. Buffered I/O patterns (READ_CHUNK, MAX_BUFFER, RECV_AGAIN semantics). Cap'n Proto schema design and C++ code generation.

**Containerization (intermediate):** Docker CLI internals (`run`, `exec`, `stop`, `rm`, `network`, `compose`). Container lifecycle management, pooling, and timeout-based pruning. Docker Compose network naming conventions. seccomp/capabilities security filtering.

**Documentation & Design (intermediate):** Technical specification writing with Mermaid diagrams (graph TB, sequenceDiagram, C4 architecture modeling). Multi-section structured document templates. Software architecture review panel methodologies. Cross-cutting concurrency model documentation.

**Process & Methodology (intermediate):** TDD workflow (red-green-refactor). TDD with E2E harnesses (failing test first, then implementation). Code coverage analysis and gap-driven test writing. Specification-first development. Bottom-up spec revision methodology. Architectural tradeoff analysis (synchronous vs async, single-thread vs two-thread, JSON vs binary IPC). Enterprise audit and compliance analysis.

### Key Architectural & Design Patterns

**Virtual Interface Abstraction from Day One:** Every major subsystem was defined by a pure virtual interface before any concrete implementation was written. This allowed dependency injection for testing, clean substitution of implementations (e.g., `NullResourceProvider` for testing vs `SqliteResourceProvider` for production), and clear architectural boundaries. The `PersistenceStore`, `InferenceProvider`/`LlmProvider`, `ResourceProvider`, and `ToolRunner` interfaces exemplify this pattern.

**Event-Driven Architecture with MPSC Channels:** The threading architecture settled on a C2 two-thread design: the AppCoreThread owns all application state (LLM provider, tool execution, persistence) and communicates with the TUI thread exclusively through MPSC channels with eventfd wakeup. The TUI is a thin rendering client with zero core references — not even read-only persistence access. This emphatic boundary enforcement eliminated the render starvation and data race bugs that plagued earlier single-thread designs.

**Three-Process Supervision Hierarchy (a0→b1→c2):** The agent ecosystem follows a strict parent-child topology. `a0` is the per-project agent binary. `b1` is the per-workdir supervisor that registers a0 instances, detects crashes, and routes IPC messages. `c2` is the machine-level monitor that aggregates b1 registrations, serves the web dashboard, and pushes SSE events. b1 acts as a pure relay for streaming data (no process ownership of streams), while maintaining per-group aggregates for c2.

**State Machine for Core Execution Loop:** The `DrivenCore` implements a three-state state machine (Idle → AwaitingLlm → ExecutingTools) that replaced the duplicated `xRunForkedLoop` (headless)/`processGoalStreaming` (TUI) paths. Each state handles specific events: `submitGoal` transitions from Idle to AwaitingLlm, LLM response with `tool_calls` transitions to ExecutingTools, and tool completion returns to AwaitingLlm or Idle. The state machine is ticked from a `Renderer` wrapper in the TUI thread, keeping it responsive without a separate polling thread.

**Specification-First Development:** Throughout the project, architectural changes were preceded by specification documents. The concurrency model spec, technical specification revisions, and implementation plans were written before code was modified. The bottom-up spec update on June 9 demonstrated the maturity of this approach — many spec files were brought into alignment with the codebase after months of rapid development.

**Persistence-First I/O Architecture:** Late in the project, a fundamental shift moved from event-driven to stream-based architecture. Tool outputs and LLM responses are persisted as streams of chunks with resource handles, rather than as monolithic events. The `ResourceProvider` abstraction (with SQLite and null implementations) provides cursor-based access to resources identified by handles. This enables the TUI to lazily load and display resources, and provides the foundation for deterministic replay.

**Bottom-Up Specification Revision:** When spec files drifted from the codebase, the revision strategy was strictly bottom-up: individual file-level `.spec.md` files first, then sub-module `technical-specification.md` aggregators, then the root `technical-specification.md`. This ensured that every source file had a corresponding spec and that higher-level documents were accurate reflections of the leaf-level contracts.

**Incremental Refactoring with Intermediate States:** The project repeatedly used intermediate architectural states as stepping stones. For example, the agent core went through `AgentCore` → `DefaultAgentCore` with forked loop → `DrivenCore` state machine. The system tools went through compiled-in handlers → `SystemToolRegistry` class → merged into `SkillManager`. The build system went from monolithic `a0_lib` → five libraries → twelve libraries. Each intermediate state was fully functional and tested before the next transformation.

**Convention Migration as Infrastructure:** Qualified name separators changed twice during the project — from `:` to `-` to `_`. Each migration was treated as systematic infrastructure work with automated tooling (sed scripts for many renames, find/sed for hundreds of string literals) rather than manual editing. The scope of each migration was defined by checking the full call chain from entry point to handler dispatch.

**Test Infrastructure as Product:** The testing infrastructure received the same architectural rigor as the production code. The Python PTY-based `TuiDriver`, the `MockServer` with scenario-driven multi-turn conversations, the fixture generation pipeline from real session exports, and the Playwright browser bridge were all independently designed, documented, and versioned. The E2E test suite was treated as a primary consumer of the API, often revealing bugs before any other mechanism.

### Prompter–Model Collaboration Patterns

The collaboration style evolved significantly over the thirteen days but maintained a consistent division of labor. The model performed virtually all code generation, implementation, test writing, debugging instrumentation, documentation drafting, and build system configuration. The prompter's role was architectural direction, scope definition, bug identification from observed behavior, strategic tradeoff decisions, and quality enforcement.

In early sessions (May 28–29), the prompter gave broad specifications ("implement all 9 modules") and the model produced large batches of code, followed by prompter review cycles that caught bugs, coverage gaps, and design issues. By the middle phase (May 30–June 1), the pattern shifted to tighter iteration cycles — the prompter would specify a design decision, the model would implement a focused change, and the prompter would test it immediately (often by running the binary or examining test output). The enterprise panel session on June 1 was unusual in that the prompter issued a single high-level command and the model executed a complex multi-step process autonomously.

During the TUI-heavy sessions (June 4–6), the collaboration became highly interactive and debugging-intensive. The prompter would observe real TUI output, identify misbehavior (tools stuck "Running", scrolling broken, text not wrapping), and direct the model's root-cause investigation. The model would add TRACE instrumentation, analyze logs, identify the code path, and propose fixes. This pattern produced detailed bug reports within single sessions — for example, the June 5 bugfix session found multiple root causes across several files from observed streaming artifacts.

The prompter consistently rejected shortcuts and demanded architectural correctness. When the model proposed keeping `SkillManager`/`PersistenceStore` references in the TUI for convenience, the prompter insisted on the strict MPSC-only boundary. When the model wanted to add a heartbeat mechanism for interrupt rendering, the prompter identified that `RequestAnimationFrame()` alone was the correct fix. When the model suggested pre-resolving DNS as a workaround for `curl_multi` async issues, the prompter directed investigation into the actual root cause (`curl_multi_wait` before `perform`).

Review was continuous and test-gated. The prompter rarely reviewed code in detail — instead, they ran the test suite, examined test output, and judged correctness by observable behavior. The model was trusted to produce structurally sound code but was held accountable for correctness through the test suite. The TDD workflow (write failing test, then implement, then verify passing) was enforced by the prompter throughout, especially in the E2E testing sessions where the prompter insisted on writing the test before the fix.

The prompter's most valuable contributions were architectural pattern recognition (identifying when the `SchemaInferenceEngine` was dead code, when `SystemToolRegistry` was duplicating `SkillManager`, when the TUI's thread embedding violated the concurrency model), scope management (prioritizing tasks, deferring non-critical features), and real-world validation (examining session exports, running the actual TUI, reading real API responses). The model's strengths were breadth of implementation across C++, Python, JavaScript, CMake, and shell simultaneously; consistent enforcement of coding conventions; and the ability to trace data flows through complex multi-module call chains during debugging.

The collaboration was notably asymmetric in time investment. Session self-assessments estimated 135.4883 hours of prompter time and 1,243.5–1,497.5 hours of senior engineer–equivalent effort, while the gap-capped active engagement measured from timestamps totaled 15.7684 hours. The leverage came from the prompter's ability to make high-bandwidth architectural decisions in minutes that would take hours for an engineer to analyze, and the model's ability to execute those decisions across dozens of files without the overhead of context switching, code navigation, or documentation lookup.

### Aggregation Tags

**Core Architecture:** C++17, C++20, agent architecture, event-driven-architecture, state machine, three-process architecture, a0 agent, b1 supervisor, c2 dashboard, multi-process supervision, concurrency model, thread safety, MPSC channels, eventfd, poll-based event loop

**LLM Integration:** DeepSeek API, OpenAI-compatible, function-calling, tool-calling, SSE streaming, curl_multi, async HTTP, LlmProvider, DrivenProvider, ResponseDecoder, prompt engineering, base prompt design, dynamic tool accumulation, tools_for_prompt

**Build System & Tooling:** CMake, FetchContent, static libraries, build system restructuring, circular dependency resolution, ENABLE_TRACE, lcov, gcov, genhtml, coverage instrumentation, branch coverage, CLI11 argument parsing, subcommand design, incremental compilation

**Subprocess & OS:** fork/exec/waitpid, POSIX signals, SIGALRM, process groups, pipe/dup2, PTY allocation, pseudoterminal, setsid, daemonization, stderr capture, stdin piping, process supervision, kill-by-process-name

**Persistence & Storage:** SQLite, WAL mode, schema migration, PersistenceStore, SqliteStore, sub-session tracking, deterministic replay, ReplayEngine, persistence-first I/O, ResourceProvider, SqliteResourceProvider, stream-based resources, Cap'n Proto

**Testing:** Google Test, GTest, TDD, unit testing, integration testing, E2E testing, code coverage, test-driven development, Python PTY testing, TuiDriver, MockServer, mock DeepSeek server, scenario-driven testing, fixture generation, Playwright, headed-browser automation, shadow DOM bridge, pytest, ctest

**Terminal UI:** FTXUI, TUI, terminal user interface, message panel, input panel, status bar, markdown renderer, markdown rendering, MD4C, dialog manager, clipboard, OSC 52, xclip, scrollable viewport, word wrapping, auto-scroll, collapsible tool calls, click-to-toggle, ftxui::reflect, bracketed paste, mouse event routing, alternate screen

**Docker & Containers:** Docker CLI, container pooling, trust-level tiers, Docker Compose, container lifecycle, idempotent apt install, DockerSecurityFilter, sandbox isolation, session-aware container naming, compose orchestration, multi-stage build

**IPC & Networking:** Unix domain sockets, JSON-line framing, IPC protocol, BufferedSocket, SSE, Server-Sent Events, uWebSockets, HTTP REST API, event push, client ping/pong, WebComponents, SPA, xterm.js, MIME detection

**Skill System:** Skill ecosystem, skill manifests, three-tier namespace, system skills, local skills, github namespace, SkillManager, SkillLoader, skill discovery, DependencyGraph, ToolState, parallel tool execution, handler registry, unified dispatch, schema validation, valijson, JSON Schema Draft-07, template substitution, parameter expansion

**Refactoring & Architecture:** Architectural refactoring, dead code removal, interface extraction, spec-first design, bottom-up spec revision, convention migration, separator migration, qualified names, build graph mapping, include dependency analysis, library boundary enforcement, deprecation strategy, source tree reorganization

**Infrastructure & DevOps:** Git, git worktree, git rebase, atomic commits, worktree-based development, session-based branches, branching workflow, cleanup scripts, zombie process cleanup, pkill, PID files, GH CLI, GitHub Issues, GitHub Projects, GraphQL API, project board automation, milestone management

**Documentation & Specifications:** Technical specification, .spec.md, Mermaid diagrams, C4 architecture modeling, sequence diagrams, structure documentation, developer guides, AGENTS.md, concurrency model specification, expert panel review, architecture review, specification audit, spec staleness detection, git-audit

**Security & Compliance:** zero-trust security model, JWT authentication, auth boundaries, auth audit log, Docker sandbox, security filtering, enterprise audit, SOC2, GDPR, ISO 27001, security compliance
