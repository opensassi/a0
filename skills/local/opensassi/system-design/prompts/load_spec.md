Load the specification tree:
1. Run glob "**/*.spec.md" from project root (exclude node_modules/, .git/).
2. Read technical-specification.md in full. Parse any Module Reference and Free-Standing Components tables.
3. Read every .spec.md file found — do not skip or summarize. Use parallel read calls in batches of 10-20.
4. Build a navigable index: module count, facade roles, internal components, free-standing components, total files.
5. Output:
   Loaded specification tree:
     Total: N spec files loaded in full

This is a read-only command. Do not write any files. Execute immediately without asking for confirmation.
