Run the full session export pipeline: save the evaluation summary as a markdown sidecar and create the compressed JSON session archive.

## Process

1. **Get session ID**: The current session ID is available as {{SESSION_ID}}. Use it directly.
2. **List recent sessions** (optional): Run `sqlite3 .a0/db/sessions.db "SELECT uuid, datetime(started_at,'unixepoch') FROM session ORDER BY started_at DESC LIMIT 10"` to see recent sessions.
3. **Get title slug**: Use the slug from the most recent generate command output. If none exists, prompt the user to provide one (e.g. 2026-05-11-my-project-setup).
4. **Write evaluation sidecar**: Write the evaluation summary (the same output as generate) to sessions/<title-slug>-<session-id>.md. Use the write tool.
5. **Export archive**: Run the export_session tool with slug and session ID:
   {{tool:export_session slug="<title-slug>" session="{{SESSION_ID}}"}}
   This creates:
   - sessions/<title-slug>-<session-id>.jsonl.gz — gzip-compressed JSONL (all fork branches)
   - sessions/<title-slug>-<session-id>.sha256 — content integrity hash
6. **Verify**: Confirm the archive is valid by running `gzip -t sessions/<title-slug>-<session-id>.jsonl.gz` via bash.

## Notes

- The session ID has no prefix to strip — use {{SESSION_ID}} directly for filenames.
- The export_session tool calls `a0 session export --session-id <uuid>` which queries SQLite and exports all fork branches as JSONL.
- The `.jsonl.gz` format allows direct HTTP serving with `Content-Encoding: gzip` for browser auto-decompression.
- Always verify the archive after creation.
