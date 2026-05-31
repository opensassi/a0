# SkillManager Spec

## 1. Overview

Public facade for the Skills sub-module. Manages the three-tier namespace (system/local/github), loads skill manifests from disk, resolves qualified names for tools and prompts, and provides lifecycle operations (install, remove, gc, validate).

**Source files:** `src/skills/skill_manager.cpp`

**Dependencies:** `SkillLoader`, `VersionManager`, `ValidationEngine`, `CommandRunner`

## 2. Component Specifications

```cpp
class SkillManager {
public:
    SkillManager(const std::string& skillsRoot,
                 const std::string& storeRoot,
                 a0::persistence::PersistenceStore* persistence = nullptr);
    ~SkillManager();

    int loadAll();
    int getTool(const std::string& qualifiedName, SkillTool& tool) const;
    int getPrompt(const std::string& qualifiedName, Prompt& prompt) const;
    int getManifest(SkillNamespace ns, const std::string& component, SkillManifest& manifest) const;
    int getPromptResolved(const std::string& qualifiedName, Prompt& out) const;
    int resolveName(const std::string& componentNs,
                    const std::string& componentName,
                    const std::string& shortName,
                    std::string& qualifiedOut) const;
    std::unordered_map<std::string, std::string> buildDispatchTable() const;
    std::vector<std::string> listSkills(std::optional<SkillNamespace> ns) const;
    int addTool(const std::string& component, const SkillTool& tool);
    int addPrompt(const std::string& component, const Prompt& prompt);
    int updateTool(const std::string& component, const std::string& name, const SkillTool& tool);
    int install(const std::string& sourceUrl, bool force = false);
    int install(const std::string& sourceUrl, const std::string& commit, bool force = false);
    int remove(const std::string& qualifiedName);
    int gc(bool dryRun = false);
    int validate(const std::string& qualifiedName,
                 const std::string& commit,
                 std::string& report);
};
```

## 3. Architecture

```mermaid
graph TB
    SM[SkillManager]
    SL[SkillLoader]
    VM[VersionManager]
    VE[ValidationEngine]
    CR[CommandRunner]

    SM --> SL
    SM --> VM
    SM --> VE
    VE --> CR

    subgraph Namespace_Tiers
        SYSTEM[skills/system/]
        LOCAL[skills/local/]
        GITHUB[skills/github_*/]
    end

    SL --> SYSTEM
    SL --> LOCAL
    SL --> GITHUB
    VM --> STORE[.a0/store/]
