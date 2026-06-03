# SkillLoader Spec

## 1. Overview

Directory scanner and manifest parser for the Skills sub-module. Walks the three-tier namespace directories, parses `skill.json` manifests, and maintains an in-memory index of components, tools, and prompts.

**Source files:** `src/skills/skill_loader.h/.cpp`

## 2. Component Specifications

```cpp
class SkillLoader {
public:
    explicit SkillLoader(const std::string& root);

    int loadAll();
    int getTool(const std::string& ns, const std::string& component,
                const std::string& toolName, SkillTool& tool) const;
    int getPrompt(const std::string& ns, const std::string& component,
                  const std::string& promptName, Prompt& prompt) const;
    std::vector<std::string> listComponents(SkillNamespace ns) const;
    int writeManifest(const std::string& component, const SkillManifest& manifest);
    int addTool(const std::string& component, const SkillTool& tool);
    int addPrompt(const std::string& component, const Prompt& prompt);
    int updateTool(const std::string& component, const std::string& name, const SkillTool& tool);
    int removeComponent(const std::string& component);
    int getManifest(SkillNamespace ns, const std::string& component,
                    SkillManifest& manifest) const;

    /// Validate a JSON object against the skill.json schema (Draft-07).
    /// \param json     The parsed JSON object to validate.
    /// \param errors   Output: human-readable validation errors.
    /// \retval 0   Valid.
    /// \retval -1  Invalid (errors populated).
    int validateAgainstSchema(const nlohmann::json& json, std::string& errors) const;

private:
    valijson::Schema m_schema;
    mutable valijson::Validator m_validator;
    bool m_schemaLoaded = false;

    int xLoadSchema(const std::string& schemaPath);
    int xParseManifestFile(const std::string& path, SkillManifest& manifest) const;
};
```

## 2a. Schema Validation

On construction, `SkillLoader` calls `xLoadSchema(skillsRoot + "/schema.json")` to load the Draft-07 JSON Schema from `skills/schema.json`. If the schema file is missing or unparseable, validation is disabled and a warning is printed.

Every `xParseManifestFile` call now:
1. Parses the JSON from disk
2. Calls `validateAgainstSchema(j, errors)` — if invalid, skips the component with a warning
3. Populates manifest struct from JSON (the `readManifest` method has been removed — inlined into `xParseManifestFile`)

New fields parsed:
- `SkillTool::streaming` from `"streaming": true`
- `SkillTool::subCommand` from `"subCommand": "rev-parse"`
- `Prompt::parallelValidators` from `"parallelValidators": true`

## 2b. Schema File (skills/schema.json)

Located at `./skills/schema.json`. Defines valid skill.json structure using JSON Schema Draft-07. Includes definitions for:
- `SkillTool` — name, description, command, inputMode, systemTool, default, timeoutSecs, parameters, dockerImage, trustLevel, aptDependencies, subCommand, streaming
- `SkillPrompt` — name, description, prompt/promptFile, dependencies, chain, validators, parallelValidators
- `SkillArg` — name, description, type, default, required
- Top-level `subModules` and `args` sections

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| loadAll with valid manifests | All components indexed |
| Malformed skill.json | Skipped with warning |
| addTool then getTool | Tool found in manifest |
| removeComponent | Manifest deleted from disk |
