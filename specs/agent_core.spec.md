# AgentCore Spec

## Input/Output Contract
- `init(componentsDir)`: loads components, sets up logger, creates initial components if empty
- `processGoal(goal)`: find matching skill by **exact name** (case-sensitive) → resolve transitive dependencies via DependencyResolver → SkillRunner.execute() → log → return result
- `resumeSession(sessionId)`: replay log entries to rebuild context
- `run()`: REPL loop reading stdin lines, calling processGoal, writing results

## Error Handling
- No exact skill name match → call SchemaInferenceEngine.inferSkill, then check dependencies, then execute
- Missing dependencies → returns `"Missing dependencies: dep1, dep2"` without calling LLM
- init with invalid directory → returns false
- processGoal during uninitialized state → throws `std::logic_error`

## Edge Cases
- Empty goal string → returns "no goal provided"
- Skill name is substring of goal (e.g. goal "bashful", skill "bash") → no match (exact name only)
- Ctrl+D (EOF) on run loop → exits cleanly
- Multiple rapid goals → processed sequentially, context accumulates
- Resume of non-existent session → returns false
