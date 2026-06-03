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

## MCP Tools

The following MCP servers are configured in `.opencode/opencode.json`:

### Playwright MCP — Headless Browser for Testing

Provides headless browser automation via accessibility snapshots (no vision model needed).

- `browser_navigate` — navigate to a URL
- `browser_snapshot` — capture accessibility tree of the page
- `browser_click` / `browser_type` / `browser_hover` — interact with elements
- `browser_evaluate` — run JS in page context
- `browser_take_screenshot` — capture screenshots
- `browser_console_messages` — read console output
- `browser_network_requests` / `browser_network_request` — inspect network
- `browser_fill_form` — fill multiple form fields at once
- `browser_tabs` — manage tabs
- `browser_wait_for` — wait for text or time
- `browser_drag` / `browser_drop` — drag and drop
- `browser_file_upload` — upload files
- `browser_select_option` — select dropdown options
- `browser_press_key` — press keyboard keys

### GDB Debugger MCP — C/C++ Debugging

Provides process-level debugging for C/C++ applications.

- `gdb_start_session` — start a GDB debugging session
- `gdb_set_breakpoint` — set breakpoints at functions, file:line, or addresses
- `gdb_get_backtrace` — get stack backtrace for a thread
- `gdb_get_variables` — get local variables for a stack frame
- `gdb_evaluate_expression` — evaluate C/C++ expressions
- `gdb_call_function` — call a function in the target process
- `gdb_step` / `gdb_next` — step through code
- `gdb_continue` — continue execution
- `gdb_interrupt` — interrupt a running program
- `gdb_get_registers` — view CPU registers
- `gdb_list_breakpoints` / `gdb_delete_breakpoint` — manage breakpoints
- `gdb_enable_breakpoint` / `gdb_disable_breakpoint` — toggle breakpoints
- `gdb_select_frame` / `gdb_select_thread` — navigate execution context
- `gdb_get_frame_info` — get current frame details
- `gdb_get_threads` — list all threads
- `gdb_get_status` — get session status
- `gdb_execute_command` — execute arbitrary GDB commands
- `gdb_stop_session` — stop a debugging session
