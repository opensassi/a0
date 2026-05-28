# ComponentRegistry Spec

## Input/Output Contract
- `loadFromDirectory(path)`: scans `path/*.tool.json` and `path/*.skill.json`
- `getTool(name)` / `getSkill(name)`: returns parsed component or nullopt
- `listTools()` / `listSkills()`: returns all loaded names
- `addTool(tool)` / `addSkill(skill)`: registers in-memory and writes JSON file

## Error Handling
- Directory does not exist → returns false, logs warning
- Malformed JSON → skips file, logs error, continues
- Duplicate name → last one wins, logs warning

## Edge Cases
- Empty directory → returns true with zero components loaded
- File with both `.tool.json` and `.skill.json` patterns → treated as tool
- Unicode in component names → pass through as-is
