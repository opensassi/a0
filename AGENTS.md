# opencode Agent Instructions — @opensassi/opencode

This project uses the **@opensassi/opencode** skill pack.
All skills, scripts, and tooling are delivered via the npm package.

## Available Skills

| `skill` | Use case |
|---------|----------|
| `asm-optimizer` | SIMD/assembly optimization framework |
| `daily-evaluation` | Aggregate session evaluations into dashboards |
| `demo-video` | Produce narrated demo videos with multi-language subtitles |
| `git` | Fork-based rebase workflow with skill templates. Uses run_skill('/system/git/start_session'). Browse with show_skills('/system/git') |
| `issue` | GitHub issue management |
| `npm-optimizer` | Port an npm package to a C++ native addon |
| `opensassi` | Bootstrap a new project environment |
| `profiler` | Linux perf profiling + flamegraphs |
| `session-evaluation` | Generate structured session reports |
| `skill-manager` | Create/revise skills interactively |
| `system-design` | Interactive C++ spec authoring with diagrams |
| `system-design-review` | Seven-expert panel audit of technical specs |
| `todo` | Create issues + debugging skills from session context |

## Workflow

1. `skill opensassi` — Load the bootstrap skill. It exposes the full skills-index as a reference table.
2. Run `npx @opensassi/opencode <skill-name>` to load any sub-skill. The agent reads the output as the skill's full instructions.
3. Use the skill's commands. Scripts are run via `npx @opensassi/opencode run <path>` or `npx @opensassi/opencode run --skill <name> <path>`.

## Design Constraints

- No commits during development — all changes staged at finish-session time
- Single atomic commit per session
- Full test suite after every rebase — run `bash test/e2e/run_all_tests.sh` (see §11.8 in technical-specification.md)
- Session evaluation is read-only (generate) / write-once (export)
- All skills, scripts, and AGENTS.md live in the npm package, not in the project

## Rules for Debugging Test Failures

When a test fails, follow this process exactly. These rules MUST BE FOLLOWED IN ANY CASE OF A TEST FAILURE. Do not skip steps. Do not guess. Do not make multiple changes at once.

### Rule 1: Observe Before Changing

Reproduce the failure exactly — run the SAME test command, not a modified version. Use the test's own infrastructure (fixtures, mocks, drivers) to avoid introducing new variables.

### Rule 2: Instrument, Don't Guess

Add targeted debug output at key code paths before forming any hypothesis. The following instrumentation tools are available:

- **TRACE_LOG** — Add `TRACE_LOG("message")` at critical checkpoints. C++ `TRACE_LOG` writes to stderr, which is captured by the `--log-dir` mechanism at runtime. Enable TRACE with: `cmake -B build -DENABLE_TRACE=ON && cmake --build build`.
  - **IMPORTANT**: Verify that TRACE is actually compiled into all relevant targets. `ENABLE_TRACE` must be added to `target_compile_definitions(<target> PRIVATE TRACE)` for each library that contains `TRACE_LOG` calls. The build system does NOT automatically propagate this to all targets.
- **GDB backtrace** — Attach `gdb -batch -ex "thread apply all bt" -p <pid>` to see what every thread is doing. Do not just check the main thread.
- **Strace** — Use `strace -p <pid> -f -e trace=ppoll,read,write,writev` to see syscall patterns (busy-waiting, blocking I/O, stuck in poll).

### Rule 3: Isolate the Failure Source

Compare the failing path against a working path. For example, if `a0 run` works but `a0 tui` doesn't, the curl transfer code is identical — the difference is in event propagation, rendering, or thread coordination. Eliminate variables by testing the same components in different contexts.

### Rule 4: One Change at a Time

When a fix doesn't work, revert it before trying another approach. Making multiple changes simultaneously makes it impossible to know which one caused the result. Test each change individually.

### Rule 5: Follow the Data

Add trace output at every layer of the data flow: production → sending → receiving → processing → rendering. If events are produced but not displayed, trace every intermediate step:
- Is the source producing data?
- Is the transport delivering it?
- Is the receiver processing it?
- Is the display layer rendering it?

If any layer is silent, that is the bug.

### Rule 6: When the Architecture Seems Correct but the Test Fails, Re-read the Spec

The concurrency model spec (`specs/concurrency-model.md`) and sub-module specs (`src/*/technical-specification.md`) define the intended architecture. If observed behavior contradicts the spec, the implementation is wrong — not the test. If the spec has been updated to match the implementation, verify both are consistent.

### Rule 7: Clean Up Zombie Processes

Run `bash scripts/cleanup-dev.sh` **before and after every test run** to prevent stale processes from interfering with results.

Failing tests often leave behind child processes that consume CPU or block ports. To verify a clean environment, run the cleanup script and confirm its output says `cleanup-dev: done`:

```bash
bash scripts/cleanup-dev.sh
```

If cleanup-dev.sh finds nothing to clean (ideal state), the environment is ready. If it reports any signals sent, a previous run left orphans — proceed with the test but be aware of potential state contamination.

This script uses path-prefixed `pkill` to target only `build/{a0,b1,c2}` and `mock_deepseek` processes, avoiding the opencode harness itself. It also reads `.a0/*.pid` files and unlinks stale sockets. See `scripts/cleanup-dev.sh` for details.

### Rule 8: Document the Root Cause

Once found, record the root cause in a bullet point with the file path and a one-line description. This builds a library of failure modes for future debugging sessions.

## Viewing Session Exports

Export a session's full message log (including system prompt and tool definitions) using the session UUID or an 8-char prefix slug:

```bash
# Full UUID
a0 session export --session-id=5e220a713f4c03ffad7cd4fd5f61cd7d

# 8-char prefix slug
a0 session export --session-id=5e220a71

# Pipe through jq for formatted viewing
a0 session export --session-id=5e220a71 | jq .
```

The output is JSON Lines (one record per line). The first record is `{"_meta":true,...}` containing the system prompt and tool definitions. Subsequent records are individual messages with `role`, `content`, `tool_calls`, etc.

## Creating Mock Fixtures from Real Sessions

When asked to create a TUI E2E test fixture from a real agent session, use this workflow:

1. The user provides the session slug (8-char prefix from the TUI status bar, e.g. `80380ff7`)
2. Generate a short 3-5 word snake_case description from the current context that describes the flow being tested (e.g. `word_wrapping_long_tool_output`)
3. Export the session and pipe through the conversion script:
   ```bash
   a0 session export --session-id=<uuid-slug|full-uuid> | python3 scripts/session-to-fixture.py \
       --name "<3-5 word snake_case description>" \
       > test/e2e/fixtures/user_<snake_case_name>.json
   ```
4. The script filters out init-phase messages (`sub_session_id < 0`), saves tool outputs as SHA256-named files in `test/e2e/fixtures/<name>/`, rewrites tool calls to `read()` those fixture files, and emits a scenario JSON compatible with `MockServer(scenario=path, stream=True)`
5. Generate a `pytest` test method referencing the fixture, following the patterns in `test/e2e/test_tui_e2e.py`
6. Verify with: `bash test/e2e/test_tui_e2e.sh`
