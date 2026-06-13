**Session ID:** 2026-06-13-100-cpu-spin-fix

**Date / Duration:** June 13, 2026; prompter active ≈ 1.5 hours

**Project / Context:**
Debugging and fixing a 100% CPU spin / app lockup in the a0 C++ agent's terminal UI on Linux. The investigation involved building a Python-based stress test harness, using perf/strace/GDB to profile the running process, identifying a shared_ptr use-after-free in FTXUI DOM rendering, and implementing the fix across multiple files.

**Top-Level Component:**
A root-cause analysis and fix for the a0 TUI intermittent 100% CPU spin.

**Second-Level Modules:**
- Stress test infrastructure (`test_cpu_spin.py`, multi-goal session loop with CPU monitoring)
- Diagnostic scripts (per-thread /proc/stat sampling, EINTR tracking in nanosleep, per-second CPU aggregation)
- SIGSEGV handler analysis (FTXUI empty handler behavior, core dump generation, terminal restoration)
- Vector reallocation bug in `message_panel.cpp` (`toolHits` vector — `reflect()` creates dangling `Box&` on reallocation)
- CMake `ENABLE_TRACE` fix (compile definitions not propagated to subdirectory targets)
- `AppCoreThread` wakeupFn removal (TUI self-timed rendering)
- Session evaluation document

**Prompter Contributions:**
Guided the diagnostic strategy at each dead end: insisted on profiling rather than guessing, directed the strace analysis that revealed the SIGSEGV signal storm, pushed back on the stale O_NONBLOCK stdout theory and the `RequestAnimationFrame` approach, and directed the final vector reallocation investigation that found the true root cause.

**Model Contributions:**
Built the stress test harness, added TRACE_LOG instrumentation across 4 files, ran and interpreted perf/strace/GDB diagnostics, traced the 0x4e00000008 address to the `Box` struct layout, identified the `toolHits` vector reallocation pattern, implemented the fix, and wrote the post-mortem document.

**Prompter Time Estimate:**
- Reading and digesting model responses: ~40 min
- Thinking, strategizing, and weighing options: ~30 min
- Writing messages and directives: ~20 min
- **Total: ~1.5 hours**

**Model-Equivalent SME Time Estimate:**
~16 hours of a senior C++ systems engineer with signal-handling and FTXUI experience:
- Build stress test infrastructure: 3h
- Run diagnostics (perf, strace, GDB): 2h
- Analyze signal storm/use-after-free: 4h
- Design and implement fix: 4h
- Test and verify: 3h

**Required SME Expertise:**
- Linux signal handling (SIGSEGV, signal delivery, async-signal-safety)
- C++ shared_ptr control block layout and use-after-free debugging
- FTXUI DOM node lifecycle and `reflect()` reference semantics
- `perf record`/`perf report` interpretation
- `strace` syscall profiling and `rt_sigreturn` storm detection
- GDB backtrace analysis for recursive destruction patterns
- CMake build system (compile definitions, target propagation, INTERFACE libraries)
- PTY/TUI rendering and terminal escape sequence handling

**Aggregation Tags:**
100%-cpu-spin, use-after-free, ftxui, shared_ptr, sigsegv, signal-storm, strace, perf, gdb, vector-reallocation, tui-debugging, cmake-trace, terminal-restore

---
## Extracted Session Stats

- **Duration:** 20757s (345.9m)
  - First message: 09:07:18
  - Last message:  14:53:15
- **Messages:** 337 total (26 user, 311 assistant)
- **Tool call parts:** 315
- **Words:** 10,966 assistant, 4,798 user

### Tokens & Cost

| Metric | Value |
|--------|-------|
| Input Tokens — Total | 87,270,529 |
| Input Tokens — Cached | 86,134,656 (98.7%) |
| Input Tokens — Uncached | 1,135,873 |
| Output Tokens | 87,294 |
| Reasoning Tokens | 126,086 |
| Total Billed | 87,483,909 |
| Cost | $0.459946 |

### Tool Usage

| Tool      | Calls | % |
|------------|-------|---|
| bash      |   158 |  50.2% |
| edit      |    66 |  21.0% |
| read      |    65 |  20.6% |
| todowrite |     9 |   2.9% |
| write     |     7 |   2.2% |
| grep      |     7 |   2.2% |
| task      |     2 |   0.6% |
| glob      |     1 |   0.3% |

### Mode & Finish

| Mode | Count | % |
|------|-------|---|
| build | 278 | 89.4% |
| plan | 33 | 10.6% |

| Finish Reason | Count | % |
|---------------|-------|---|
| tool-calls | 286 | 92.9% |
| stop | 22 | 7.1% |

### Prompter Active Time (gap-based)

- **Prompter active:** 22.0m
- **Wall clock:** 345.9m
- **Idle/waiting:** 324.0m
- **Gaps >60s (capped):** 18 of 25

| Gap Range | Count |
|-----------|-------|
| 15-30s | 4 |
| 30-45s | 1 |
| 45-60s | 2 |
| >60s | 18 |
