# HandlerResult Spec

## 1. Overview

Lightweight return type for all C++ system tool handler functions. Carries the output string and an optional list of recommended tool names used by `tools_for_prompt` for dynamic tool accumulation.

**Source file:** `src/handler_results.h`

## 2. Component Specification

```cpp
namespace a0 {

struct HandlerResult {
    std::string output;              // Tool stdout or error message
    std::vector<std::string> recommendedTools;  // Tool names for dynamic accumulation
};

} // namespace a0
```

## 3. Usage

- Most handlers return `HandlerResult{output}` with an empty `recommendedTools` vector
- `xToolsForPrompt` populates `recommendedTools` with validated tool names that `AgentCore` inserts into `m_accumulatedTools`
- `SkillManager::executeTool()` extracts `.output` and discards recommendations
- `SkillManager::executeToolWithMeta()` returns the full struct for consumers that need recommendations

## 4. Testing Requirements

| Test | Verification |
|------|-------------|
| Default construction | `output.empty()`, `recommendedTools.empty()` |
| Output-only result | `HandlerResult{"ok"}` — output populated, recommendedTools empty |
| Result with recommendations | `HandlerResult{"plan", {"tool1", "tool2"}}` — both fields populated |
