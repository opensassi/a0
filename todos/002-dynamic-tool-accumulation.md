# 002 — Dynamic Tool Accumulation and Schema Validation

## Objective

Replace the static `schemas()` + `isSystemTool` filter system with a dynamic tool accumulation model. Tools become available to the LLM through a verified discovery process — `tools_for_prompt` generates structured JSON with schemas, C++ validates the output, and only validated tools are added to subsequent function-calling turns.

---

## Design

### Flow

```
Turn 1: combinedSchemas = schemas() (9 base tools)
        LLM calls tools_for_prompt(prompt="create_local_cli_skill npm")
          → LLM outputs JSON with intent, plan, tools[]
          → C++ validates each tool's schema against actual schemas
          → validated tool names added to m_accumulatedTools

Turn 2: combinedSchemas = schemas() + accumulatedTools
        LLM can call any tool from the validated set
```

### tools_for_prompt JSON output

The LLM is instructed to output a JSON block containing:
- `intent`: one-line summary
- `plan`: natural language execution steps
- `tools[]`: array of objects, each with:
  - `name`: tool/skill name
  - `schema`: JSON Schema object matching the tool's actual parameter structure (property names, types, required)

Validation checks each entry's generated schema against the actual schema. A mismatch rejects the entire response.

---

## Files to Modify

### 1. `src/system_tools/registry.h`

**Add to `SystemToolResult`:**
```cpp
struct SystemToolResult {
    std::string output;
    std::vector<std::string> recommendedTools;
};
```

### 2. `src/system_tools/registry.cpp` — `schemas()`

Add `read`, `glob`, `grep`, `edit`, `write` with full JSON Schema parameter definitions. Each already has a handler registered — they just need schemas.

**`read` schema:**
```cpp
result.push_back({
    "read",
    "Read files or directories from the local filesystem",
    {{"type", "object"},
     {"properties", {
         {"file_path", {{"type", "string"}, {"description", "The absolute path to the file or directory to read"}}},
         {"offset",   {{"type", "number"}, {"description", "The line number to start reading from (1-indexed)"}, {"default", 1}}},
         {"limit",    {{"type", "number"}, {"description", "Maximum number of lines to read"}, {"default", 2000}}}
     }},
     {"required", {"file_path"}}}
});
```

**`glob` schema:**
```cpp
result.push_back({
    "glob",
    "Fast file pattern matching tool",
    {{"type", "object"},
     {"properties", {
         {"pattern", {{"type", "string"}, {"description", "Glob pattern (e.g. **/*.js)"}}},
         {"path",    {{"type", "string"}, {"description", "Directory to search in"}}}
     }},
     {"required", {"pattern"}}}
});
```

**`grep` schema:**
```cpp
result.push_back({
    "grep",
    "Fast content search using regular expressions",
    {{"type", "object"},
     {"properties", {
         {"pattern", {{"type", "string"}, {"description", "The regex pattern to search for"}}},
         {"path",    {{"type", "string"}, {"description", "Directory to search in"}}},
         {"include", {{"type", "string"}, {"description", "File pattern (e.g. *.cpp, *.{ts,tsx})"}}}
     }},
     {"required", {"pattern"}}}
});
```

**`edit` schema:**
```cpp
result.push_back({
    "edit",
    "Performs exact string replacements in files",
    {{"type", "object"},
     {"properties", {
         {"file_path",   {{"type", "string"}, {"description", "The absolute path to the file to modify"}}},
         {"old_string",  {{"type", "string"}, {"description", "The text to replace"}}},
         {"new_string",  {{"type", "string"}, {"description", "The text to replace it with"}}},
         {"replace_all", {{"type", "boolean"}, {"description", "Replace all occurrences (default false)"}, {"default", false}}}
     }},
     {"required", {"file_path", "old_string", "new_string"}}}
});
```

**`write` schema:**
```cpp
result.push_back({
    "write",
    "Writes content to a file on the local filesystem",
    {{"type", "object"},
     {"properties", {
         {"file_path", {{"type", "string"}, {"description", "The absolute path to the file to write"}}},
         {"content",   {{"type", "string"}, {"description", "The content to write to the file"}}}
     }},
     {"required", {"file_path", "content"}}}
});
```

### 3. `src/system_tools/discovery_handlers.cpp` — `xToolsForPrompt`

**System prompt for tools_for_prompt:**
The inference prompt must instruct the LLM to output structured JSON with tool schemas:

```
Analyze the user's request and recommend skills/tools.

User request: "<promptText>"

<tool inventory from SkillManager>

Output JSON with this structure:
{
  "intent": "<one-line summary>",
  "plan": "<step-by-step execution plan>",
  "tools": [
    {
      "name": "<tool_name>",
      "schema": {
        "type": "object",
        "properties": {
          "<param_name>": { "type": "<string|boolean|number>", "description": "<purpose>" }
        },
        "required": ["<param_name>", ...]
      }
    }
  ]
}

Each tool's schema must exactly match the actual parameter structure of that tool.
Tool names must come from the available skills/tools listed above.
```

**Parsing logic:** After getting the LLM response, extract the JSON block (fenced ` ```json ... ``` `), parse it, and for each entry:
1. Look up the tool name in `schemas()` → get the actual `ToolSchema`
2. Compare the LLM's generated schema against the actual schema:
   - Every param in LLM's `properties` must exist in the actual schema's properties
   - Every param in actual schema's `required` must be in the LLM's `required`
   - Types must match for each param
3. If all entries pass → populate `recommendedTools` with the tool names
4. If any entry fails → return `{"recommendedTools": []}` — empty set, caller should retry

Return both the plan text (as output) and the validated tool names (in recommendedTools).

### 4. `src/agent_core.h`

```cpp
// Private members:
std::unordered_set<std::string> m_accumulatedTools;
```

### 5. `src/agent_core.cpp` — Forked loop changes

**After `tools_for_prompt` auto-injection (line ~193):**
```cpp
if (m_systemTools) {
    auto analysis = m_systemTools->execute("tools_for_prompt", {{"prompt", userInput}});
    if (!analysis.output.empty()) {
        messages.push_back({"assistant", analysis.output});
        for (const auto& t : analysis.recommendedTools)
            m_accumulatedTools.insert(t);
    }
}
```

**When building `combinedSchemas` (line ~198):**
Replace the existing `isSystemTool`-based filter with:
```cpp
std::vector<ToolSchema> combinedSchemas = tools;  // schemas() = 9 base tools

// Add accumulated tools (validated via tools_for_prompt)
for (const auto& shortName : m_accumulatedTools) {
    bool alreadyIn = false;
    for (const auto& ts : combinedSchemas)
        if (ts.name == shortName) { alreadyIn = true; break; }
    if (alreadyIn) continue;
    
    auto dit = m_dispatch.find(shortName);
    if (dit != m_dispatch.end()) {
        ToolSchema ts;
        ts.name = shortName;
        a0::skills::SkillTool st;
        if (m_skillMgr && m_skillMgr->getTool(dit->second, st) == 0)
            ts.description = st.description;
        ts.inputSchema = {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}};
        combinedSchemas.push_back(ts);
    }
}
```

### 6. `src/system_tools/registry.cpp` — `isSystemTool()`

Remove `"run_skill"` from the `coreTools` list (previously removed from schemas but still in the filter list):
```cpp
static const std::vector<std::string> coreTools = {
    "bash", "read", "glob", "grep", "edit", "write",
    "show_skills", "show_skill_tools", "tools_for_prompt"
};
```

### 7. Spec updates

- `src/base_prompt.spec.md` — already updated
- `src/system_tools.spec.md` — update: schemas() is 9-tool anchor set; dynamic accumulation model documented
- `src/agent_core.spec.md` — update: m_accumulatedTools added, combinedSchemas logic changed, tools_for_prompt structured output
- Root `technical-specification.md` — update processGoal flow, CLI startup sequence

---

## Testing

1. Build with `make -j$(nproc)` — verify compilation
2. Run `ctest` — all 27 tests must pass
3. Manual test with `./build/a0 run "create_local_cli_skill" --params '{"program":"npm"}'` — verify dynamic discovery works

---

## Verification

- `schemas()` returns 9 tools (bash, read, glob, grep, edit, write, show_skills, show_skill_tools, tools_for_prompt)
- `tools_for_prompt` output validates schemas before allowing tool use
- Forked loop `combinedSchemas` = schemas() + accumulatedTools only
- No `isSystemTool` filter on combinedSchemas
- `"run_skill"` removed from coreTools in isSystemTool()
