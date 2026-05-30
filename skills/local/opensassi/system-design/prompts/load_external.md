Load an external project's spec tree into context.

1. Verify external/<name>.md and external/<name>/ exist.
2. Read external/<name>.md in full.
3. If external/<name>/technical-specification.md exists, read it and all .spec.md files it references.
4. Record integration edges (cross-references between project-root files and this external spec).
5. Report: "Loaded external spec tree for <name> — N files, M integration edges."

If no name is given, run list_external first and ask the user to pick one.
