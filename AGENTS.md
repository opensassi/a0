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

Failing tests often leave behind child processes that consume CPU or block ports. After each test iteration, verify that all child processes are dead:

```bash
pgrep -f "a0" | grep -v $$ | xargs kill -9 2>/dev/null
pgrep -f "mock_deepseek" | xargs kill 2>/dev/null
```

### Rule 8: Document the Root Cause

Once found, record the root cause in a bullet point with the file path and a one-line description. This builds a library of failure modes for future debugging sessions.
