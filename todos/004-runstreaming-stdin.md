# Todo: Add stdin piping to CommandRunner::runStreaming

**Created:** 2026-06-03
**Session:** Skill system implementation — streaming, parallel execution, Playwright E2E
**Priority:** medium

## Context

`CommandRunner::runStreaming()` launches a subprocess with streaming output via `StreamCallback`, but **does not accept stdin data**. Its synchronous counterpart `CommandRunner::run()` accepts `stdinData` and pipes it to the child process. The streaming variant has no stdin parameter, making it unusable with `inputMode: "stdin"` command tools.

This was discovered during testing of `SkillManager::executeToolStreaming()` — the handler-based streaming path works (sync fallback), but command tool streaming via `ToolRunner::runStreaming()` produces no output when the tool uses `inputMode: "stdin"` because there's no way to pass stdin data to the streaming subprocess.

## Current state

```cpp
// command_runner.h — the streaming variant:
static StreamHandle runStreaming(const std::string& cmd,
                                   StreamCallback onChunk,
                                   int timeoutSecs = 30);
// No stdinData parameter!

// The sync variant has it:
static CommandResult run(const std::string& cmd,
                          const std::string& stdinData = "",
                          int timeoutSecs = 30);
```

## What needs to change

### 1. Add `stdinData` parameter to `CommandRunner::runStreaming`

```cpp
static StreamHandle runStreaming(const std::string& cmd,
                                   StreamCallback onChunk,
                                   int timeoutSecs = 30,
                                   const std::string& stdinData = "");
```

### 2. Implement stdin piping in the streaming child process

In `command_runner.cpp`, the `runStreaming` function currently:
1. `fork()` → child: `execlp("sh", "sh", "-c", cmd.c_str(), nullptr)`
2. Parent: reads stdout/stderr via pipe in a thread, calls `onChunk`

It needs to also:
1. Create a pipe for stdin (parent→child)
2. In the child, duplicate the stdin pipe read end to `STDIN_FILENO` before exec
3. In the parent thread (or a separate thread), write `stdinData` to the stdin pipe write end, then close it

### 3. Update `ToolRunner::runStreaming` default implementation

In `tool_runner.cpp`, the default `ToolRunner::runStreaming` implementation builds the command and delegates to `CommandRunner::runStreaming`. It needs to pass `stdinData` along:

```cpp
a0::StreamHandle ToolRunner::runStreaming(const Tool& tool, ...) {
    // ...build command...
    std::string stdinData;
    if (tool.inputMode == "stdin") {
        stdinData = params.dump();
    }
    return CommandRunner::runStreaming(cmd, std::move(onChunk), tool.timeoutSecs, stdinData);
}
```

### 4. Wire `SkillManager::executeToolStreaming` to pass stdin

Already partially wired — `executeToolStreaming` calls `runner->runStreaming(legacy, params, onChunk)`. The `legacy` Tool struct carries `inputMode`, and `SubprocessToolRunner::runStreaming` (or the default `ToolRunner::runStreaming`) needs to use it.

But wait — looking at the current code paths:

- `SubprocessToolRunner::runStreaming` is defined in `tool_runner.cpp` as the **default** `ToolRunner::runStreaming` override
- It builds the command and calls `CommandRunner::runStreaming(cmd, onChunk, tool.timeoutSecs)` — no stdin
- It needs updating to compute `stdinData` from `params` based on `tool.inputMode`

### 5. Test

Add a test to `test/unit/test_tool_runner.cpp`:

```cpp
TEST_F(ToolRunnerTest, RunStreamingWithStdin) {
    Tool tool = makeBashTool();
    tool.inputMode = "stdin";
    tool.command = "cat";  // echo stdin to stdout
    std::string accumulated;
    a0::StreamHandle handle = runner.runStreaming(tool,
        {{"input", "hello via stdin"}},
        [&](const std::string& data, const std::string&) { accumulated += data; });
    handle.wait();
    EXPECT_EQ(accumulated, "hello via stdin\n");
}
```

Also add a test through `SkillManager::executeToolStreaming` for a command tool with `inputMode: "stdin"` once the runner is fixed (currently the test in `test_skill_manager.cpp` uses a handler shield to avoid this gap).

## Files to modify

| File | Change |
|------|--------|
| `src/command_runner.h` | Add `std::string stdinData = ""` param to `runStreaming` |
| `src/command_runner.cpp` | Implement stdin pipe: create pipe before fork, write stdinData in parent after fork, close write end |
| `src/tool_runner.cpp` | Default `ToolRunner::runStreaming`: compute stdinData from params based on inputMode, pass to CommandRunner::runStreaming |
| `src/tool_runner.h` | No changes (signature inherited from virtual) |
| `test/unit/test_tool_runner.cpp` | Add `RunStreamingWithStdin` test |
| `test/unit/test_skill_manager.cpp` | Uncomment/reenable `ExecuteToolStreaming_CommandToolViaRunner` test |

## Dependencies

- `CommandRunner::xRunSingle` in `command_runner.cpp` has the existing stdin piping implementation — can be used as reference for the streaming variant
- The `fork()` / `exec()` / `pipe()` pattern is already well-established in the codebase

## Notes

- The pipe must be created **before** `fork()` so both parent and child have access
- The parent should write stdinData in a separate thread (or before starting the reader thread) to avoid deadlocks
- If stdinData is empty, the pipe write end should be closed immediately after fork so the child's stdin reads EOF
- Timing: the stdin write can happen concurrently with the stdout read — stdout may start arriving before stdin is fully written
