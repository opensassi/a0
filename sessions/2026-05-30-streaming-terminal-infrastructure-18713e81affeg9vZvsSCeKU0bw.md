**Session ID:** 2026-05-30-streaming-terminal-infrastructure

**Date / Duration:** 2026-05-30; prompter active ≈ 4.5 hours

**Project / Context:**
Design and implementation of a streaming tool execution and web-terminal infrastructure for the a0 C++17 agent ecosystem. The project spans five sub-modules (a0 agent, b1 supervisor, c2 dashboard, IPC protocol, persistence layer) and requires adding concurrent process execution, PTY allocation, SSE event streaming, and a WebComponents-based terminal UI.

**Top-Level Component:**
Streaming tool execution + web terminal — allowing tools to produce output incrementally (rather than blocking) and enabling interactive shell sessions from the c2 web dashboard.

**Second-Level Modules:**
- Persistence layer: `stream` and `stream_chunk` SQLite tables with create/append/end/query methods
- `CommandRunner::runStreaming()` — threaded fork/exec with pipe readers and `StreamHandle` (wait/cancel/sendInput)
- `StreamRegistry` — standalone thread-safe registry for active stream handles
- `ToolRunner::runStreaming()` — streaming variant on both `SubprocessToolRunner` and `DockerToolRunnerImpl`
- IPC protocol extensions — 5 new message types (`stream_data`, `stream_end`, `stream_input`, `terminal_open`, `terminal_ready`) with 7 new fields
- b1 relay handlers — pure IPC forwarding (no process ownership), stream→agent routing
- `a0 --terminal` mode — PTY allocation, shell fork, streaming loop with bidirectional IPC
- c2 REST endpoints — `POST /api/terminal/open`, `POST /api/stream/:id/input`, `GET /api/terminal/status/:terminalId`, `GET /api/session/:uuid/streams`, `GET /api/stream/:id/chunks`
- c2 SSE streaming events — `stream_chunk`, `stream_end`, `terminal_ready` broadcast
- Web UI — vendored xterm.js, `<terminal-view>` WebComponent, terminal launch UI on hosts page, `/terminal` route with hash-based params
- `killByProcessName` — `pgrep`-based daemon cleanup replacing slow `/proc` scan
- DB migration for `terminal_id` column
- Directory validation on terminal launch

**Prompter Contributions:**
Directed the overall architecture: streaming via threaded fork/exec (not event-loop), SSE+REST transport (not WebSocket), direct SQLite reads by c2 (not IPC proxy), b1 as pure relay (no process ownership), `a0 --terminal` as a full a0 instance (not a b1/c2 function). Identified and corrected path-doubling bug in b1's realpath logic, input-thread timeout bug killing the main loop, missing SSE terminal_ready broadcast, missing `terminal_id` DB migration, stale xterm.js vendor file, and CSS shadow-DOM isolation issues. Specified polling over IPC for terminal readiness, hash-based URL params for back/forward compatibility, and directory existence validation.

**Model Contributions:**
Implemented all C++ code (persistence schema, CommandRunner streaming, ToolRunner streaming, IPC protocol, b1 relay, a0 --terminal mode, c2 REST/SSE endpoints, DB migration, kill-by-pgrep), all Web UI code (xterm.js integration, terminal-view WebComponent, SSE event wiring, hosts-page terminal launcher, app.js routing), build system updates, and test fixes. Debugged and fixed 8+ runtime issues (path doubling, input thread timeout, missing migration, stale binary, atob() corruption, shadow DOM CSS isolation, uWS onAborted crash, SSE initialization ordering).

**Prompter Time Estimate:**
- Reading and digesting model responses: ~2.0 hours
- Thinking, strategizing, and weighing options: ~1.5 hours
- Writing messages and directives: ~1.0 hours
- **Total: 4.5 hours**

**Model-Equivalent SME Time Estimate:**
~40–60 hours of a senior C++/full-stack engineer working across 5 sub-modules:
- Persistence schema design and implementation: 4 hours
- Concurrent process execution infrastructure: 6 hours
- IPC protocol design and implementation: 4 hours
- b1 supervisor relay integration: 3 hours
- a0 PTY terminal mode: 5 hours
- c2 REST/SSE/DB endpoint implementation: 8 hours
- Web UI xterm.js integration and WebComponents: 8 hours
- Debugging and integration testing: 10 hours
- Architecture and design decisions: 4 hours

**Required SME Expertise:**
- C++17 systems programming (fork/exec/pipe, signal handling, PTY allocation)
- SQLite3 schema design and WAL concurrency
- Linux kernel process and terminal management (POSIX job control, pty(7))
- uWebSockets HTTP/SSE server development
- JavaScript WebComponents v1 and Custom Elements
- xterm.js terminal emulator integration
- Unix domain socket IPC protocol design (JSON-line framing)
- Thread safety and lock-free synchronization patterns
- CMake build system management
- Realpath/directory resolution semantics on Linux

**Aggregation Tags:**
C++, streaming, terminal, PTY, IPC, SSE, WebComponents, xterm.js, SQLite, fork/exec, realpath, uWebSockets, agent, Docker, process management, concurrent execution
