# DependencyResolver Spec

## Input/Output Contract
- `checkToolDependencies(tool)`: always returns true (tools have no deps)
- `checkSkillDependencies(skill)`: returns true iff `missingDependencies` is empty
- `missingDependencies(skill)`: returns list of dependency names not found in registry, **traversing the transitive closure** of skill dependencies

## Error Handling
- Unknown dependency → included in missing list
- Duplicate dependencies → deduplicated via visited set
- Circular dependencies (A → B → A) → cut by visited set, not reported as missing
- Transitive chain: if A depends on B and B depends on missing tool t, both B and t are reported

## Edge Cases
- Skill with no dependencies → empty result
- Skill depending only on tools → single-level check only
- Deep transitive chains → fully resolved
- Registry with zero components → all dependencies reported as missing
