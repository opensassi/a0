# ToolRunner Spec

## Input/Output Contract
- `run(tool, params)` executes `tool.command` via subprocess
- `inputMode=="stdin"`: params serialized to JSON, piped to stdin
- `inputMode=="args"`: params flattened to CLI arguments
- Returns `StructuredValue` — string for stdout, array for line-split output

## Error Handling
- Non-zero exit code → returns error string prefixed with `ERROR:`
- Command not found → returns `ERROR: command not found: <cmd>`
- Timeout (30s default) → returns `ERROR: timeout`

## Edge Cases
- Empty params → run with no input/args
- Very large output (>1MB) → truncate with `... (truncated)` suffix
- Special characters in params → shell-escaped
