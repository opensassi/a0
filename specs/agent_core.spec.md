# AgentCore Spec

## Input/Output Contract
- `init(componentsDir)`: loads components, sets up logger, creates initial components if empty
- `processGoal(goal)`: find matching skill → SkillRunner.execute() → log → return result
- `resumeSession(sessionId)`: replay log entries to rebuild context
- `run()`: REPL loop reading stdin lines, calling processGoal, writing results

## Error Handling
- No matching skill for goal → call SchemaInferenceEngine.inferSkill, then execute
- init with invalid directory → returns false
- processGoal during uninitialized state → throws `std::logic_error`

## Edge Cases
- Empty goal string → returns "no goal provided"
- Ctrl+D (EOF) on run loop → exits cleanly
- Multiple rapid goals → processed sequentially, context accumulates
- Resume of non-existent session → returns false
