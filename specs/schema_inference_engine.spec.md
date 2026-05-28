# SchemaInferenceEngine Spec

## Input/Output Contract
- `inferTool(description)`: calls InferenceProvider with structured prompt → parses JSON response into Tool
- `inferSkill(description)`: calls InferenceProvider → parses JSON response into Skill

## Error Handling
- LLM returns invalid JSON → retries once, then throws
- LLM returns valid JSON but missing required fields → fills defaults, logs warning
- InferenceProvider failure → propagates exception

## Edge Cases
- Very short description (1 word) → attempts inference with minimal context
- Description asking for impossible tool (e.g., "teleport") → returns best-effort JSON
- Empty description → returns error tool with description "invalid description"
