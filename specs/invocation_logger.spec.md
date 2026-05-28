# InvocationLogger Spec

## Input/Output Contract
- `log(entry)`: appends JSON line to `logs/<sessionId>.jsonl`
- `replay(sessionId, callback)`: reads all lines, deserializes, calls callback for each
- `listSessions()`: scans `logs/*.jsonl`, returns session IDs (filenames without extension)

## Error Handling
- Log file write failure → throws `std::runtime_error`
- Session not found in replay → returns false
- Corrupt JSON line in log → skips line, continues, logs error

## Edge Cases
- Empty log → replay returns true with zero callbacks
- Concurrent writes → not supported (single-threaded)
- Very large log (>100MB) → replay streams line-by-line
