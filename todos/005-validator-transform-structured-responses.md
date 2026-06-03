# Todo: ValidatorBinding::transform + Structured Multi-part Responses

**Created:** 2026-06-03  
**Session:** Skill system implementation — comprehensive review  
**Priority:** low  

## Items

### 1. ValidatorBinding::transform dead field

`ValidatorBinding` in `src/agent_interfaces.h` declares a `transform` field that is never used:

```cpp
struct ValidatorBinding {
    std::string toolName;
    std::optional<std::string> transform;  // never read anywhere
};
```

The field was intended for future JSONPath transformation bindings (per the main spec: "future: JSONPath binding"). It is serialized in `skill_loader.cpp` but `DefaultSkillRunner::runValidators` never reads it — validators always pass the full LLM output as `input`.

**Decision needed:** Remove the dead field, or implement transform binding.

If removing: change in `src/agent_interfaces.h`, `src/skills/skill_loader.cpp` (remove from both read/serialize paths), `skills/schema.json`.

If implementing: allow validators to specify an output subset via JSONPath, e.g.:
```json
{"toolName": "extract_json", "transform": "$.result"}
```

### 2. Structured multi-part responses in HandlerResult

`HandlerResult` in `src/handler_results.h` has only `output` (string) + `recommendedTools`:

```cpp
struct HandlerResult {
    std::string output;
    std::vector<std::string> recommendedTools;
};
```

This is insufficient for tools that return structured data with multiple named sections (e.g. Playwright's snapshot tools return a snapshot tree, code block, page state, console messages — all as one flat markdown document).

**Desired:** Add an optional sections map so structured data is accessible to validators without regex parsing:

```cpp
struct HandlerResult {
    std::string output;
    std::vector<std::string> recommendedTools;
    std::unordered_map<std::string, std::string> sections;  // NEW
};
```

Validators could then reference specific sections by name. The `sections` map would be populated by tool handlers (or by the executeTool framework if it auto-detects `### Section Name` patterns in the output).

**Files to modify:**

| Item | File | Change |
|------|------|--------|
| Transform | `src/agent_interfaces.h` | Remove `transform` field or implement binding |
| Transform | `src/skills/skill_loader.cpp` | Remove from parse/write or implement |
| Transform | `skills/schema.json` | Remove or update ValidatorBinding definition |
| Transform | `src/skill_runner.cpp` | Implement binding lookup in runValidators |
| Responses | `src/handler_results.h` | Add `sections` map |
| Responses | `src/skills/skill_manager.cpp` | Pass through from handler results |
| Responses | `test/unit/test_skill_manager.cpp` | Test sections passthrough |
| Responses | `test/unit/test_skill_runner.cpp` | Test validator transform binding |
