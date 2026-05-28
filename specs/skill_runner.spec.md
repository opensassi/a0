# SkillRunner Spec

## Input/Output Contract
- `expandPrompt(skill, params)`: replaces `{{tool:<name> <json-args>}}` placeholders by executing the tool
- `runValidators(skill, input)`: runs each validator sequentially, piping output as input to next
- `execute(skill, params)`: expandPrompt → InferenceProvider.complete() → runValidators

## Error Handling
- Unknown tool in placeholder → returns original placeholder text as-is
- Validator failure → returns partial result with `VALIDATOR_ERROR:` prefix
- InferenceProvider failure → propagates exception

## Edge Cases
- No `{{tool:...}}` in prompt → no eager execution, pass through directly
- Nested placeholders → not supported, treat as literal
- Empty validator chain → return LLM output unmodified
