Run the full session export pipeline: save the evaluation summary as a markdown sidecar and create the compressed JSON session archive.

## Process

1. **Get session ID**: The current session ID is available as {{SESSION_ID}}. Use it directly.
2. **List recent sessions** (optional): Run `ls logs/*.jsonl` via bash to see all available session logs.
3. **Get title slug**: Use the slug from the most recent generate command output. If none exists, prompt the user to provide one (e.g. 2026-05-11-my-project-setup).
4. **Write evaluation sidecar**: Write the evaluation summary (the same output as generate) to sessions/<title-slug>-<session-id>.md. Use the write tool.
5. **Export archive**: Run the export_session tool with slug and session ID:
   {{tool:export_session slug="<title-slug>" session="{{SESSION_ID}}"}}
   This creates:
   - sessions/<title-slug>-<session-id>.json.bz2 — compressed full session export
   - sessions/<title-slug>-<session-id>.sha256 — content integrity hash
6. **Verify**: Confirm the archive is valid by running bzip2 -t sessions/<title-slug>-<session-id>.json.bz2 via bash.

## Notes

- The session ID has no prefix to strip — use {{SESSION_ID}} directly for filenames.
- The export_session tool reads from logs/{{SESSION_ID}}.jsonl and wraps it in a JSON array before compressing.
- Always verify the archive after creation.
