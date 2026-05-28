# SkillRunner Spec

## Input/Output Contract
- `expandPrompt(skill, params)`: first replaces `{{key}}` placeholders with values from `params`, then replaces `{{tool:<name> <json-args>}}` placeholders by executing the tool
- `runValidators(skill, input)`: runs each validator sequentially, piping output as input to next; if a validator returns a string starting with `"ERROR:"`, prefixes the result with `"VALIDATOR_ERROR:"` and stops
- `execute(skill, params)`: if a `DependencyResolver` is available, checks dependencies first → on missing deps, returns error string without calling LLM; then expandPrompt → InferenceProvider.complete() → runValidators

## Error Handling
- Unknown `{{key}}` placeholder → kept as-is in output (not substituted)
- Unknown tool in placeholder → returns original placeholder text as-is
- Validator returns `"ERROR:..."` → `"VALIDATOR_ERROR: ERROR:..."` (short-circuit, remaining validators skipped)
- Missing dependencies (with resolver) → returns `"Missing dependencies: dep1, dep2"` without calling LLM
- InferenceProvider failure → propagates exception

## Edge Cases
- No `{{tool:...}}` in prompt → no eager execution, pass through directly
- No `{{key}}` in prompt → param substitution is a no-op
- Nested placeholders → not supported, treat as literal
- Empty validator chain → return LLM output unmodified
