# DependencyResolver Spec

## Input/Output Contract
- `checkToolDependencies(tool)`: always returns true (tools have no deps)
- `checkSkillDependencies(skill)`: verifies every name in `skill.dependencies` exists in registry
- `missingDependencies(skill)`: returns list of dependency names not found in registry

## Error Handling
- Empty dependencies → returns true / empty list
- Self-referencing dependency → treated as missing

## Edge Cases
- Circular dependencies between skills → each missing dep reported independently
- Dependency name with special characters → exact string comparison
- Registry with zero components → all dependencies reported as missing
