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
    int readManifest(const std::string& path, SkillManifest& manifest) const;
    int addTool(const std::string& component, const SkillTool& tool);
    int addPrompt(const std::string& component, const Prompt& prompt);
    int updateTool(const std::string& component, const std::string& name, const SkillTool& tool);
    int removeComponent(const std::string& component);
    int getManifest(SkillNamespace ns, const std::string& component,
                    SkillManifest& manifest) const;
};
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| loadAll with valid manifests | All components indexed |
| Malformed skill.json | Skipped with warning |
| addTool then getTool | Tool found in manifest |
| removeComponent | Manifest deleted from disk |
