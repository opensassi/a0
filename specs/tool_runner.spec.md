# ToolRunner Spec

## Input/Output Contract
- `run(tool, params)` executes `tool.command` via subprocess using `fork`/`exec`/`waitpid`
- `inputMode=="stdin"`: params serialized and piped to subprocess stdin via pipe
- `inputMode=="args"`: params flattened to CLI arguments appended to command string
  - Object params: each key-value → `--key=value`, special key `"_"` → positional arg (just value)
  - Non-object params: entire value passed as single positional argument
  - All args shell-escaped
- Timeout enforced via `alarm(30)` + `SIGALRM` → `kill(-pgid, SIGKILL)` on expiry
- Returns string (stdout content)

## Error Handling
- Non-zero exit code with empty stdout → returns `ERROR: command failed with exit code <N>`
- Non-zero exit code with non-empty stdout → returns stdout content (partial success)
- Fork failure → returns `ERROR: fork failed`
- Pipe failure → returns `ERROR: pipe failed`
- Timeout (30s) → returns `ERROR: timeout`

## Edge Cases
- Empty params → run with no input/args
- Very large output (>1MB) → truncate with `... (truncated)` suffix
- Special characters in params → shell-escaped
- Large stdin data → written in a loop (handles partial writes)
- Stdin writer flush → pipe closed before reading stdout
