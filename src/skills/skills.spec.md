# Skills Header Spec

## 1. Overview

Central header for the Skills sub-module. Defines data structures (SkillTool, SkillManifest, StoredVersion, InvocationRecord), qualified name helpers, and the `SkillManager` facade class. All skills source files include this header.

**Source files:** `src/skills/skills.h`, `src/skills/skill_manager.cpp`, `src/skills/CMakeLists.txt`

**Dependencies:** `agent_interfaces.h`, nlohmann/json

## 2. Data Structures

```cpp
namespace a0::persistence { class PersistenceStore; }

namespace a0::skills {

enum class SkillNamespace { SYSTEM, LOCAL, GITHUB };

struct ToolSchema { nlohmann::json input; nlohmann::json output; };

struct SkillTool {
    std::string name, description, command, inputMode = "stdin";
    ToolSchema schema;
    std::string dockerImage;
    TrustLevel trustLevel = TrustLevel::MEDIUM;
    std::vector<std::string> aptDependencies;
    bool systemTool = false;
    int timeoutSecs = 30;
};

struct CompatBridge { /* version migration tooling */ };

struct SkillManifest {
    std::string name, version, description;
    SkillNamespace ns;
    std::string sourceUrl, commitHash;
    std::vector<SkillTool> tools;
    std::vector<Prompt> prompts;
    std::vector<CompatBridge> compat;
    std::unordered_map<std::string, std::string> dependencies;
};

struct StoredVersion { /* archive entry */ };

struct InvocationRecord { /* historical tool call */ };

// Qualified name helpers
bool parseQualifiedName(...);
std::string buildQualifiedName(...);

// Forward declarations
class SkillLoader;
class VersionManager;
class ValidationEngine;

class SkillManager {
public:
    SkillManager(const std::string& skillsRoot,
                 const std::string& storeRoot,
                 a0::persistence::PersistenceStore* persistence = nullptr);
    virtual ~SkillManager();

    int loadAll();
    int getTool(const std::string& qualifiedName, SkillTool& tool) const;
    int getPrompt(const std::string& qualifiedName, Prompt& prompt) const;
    int getManifest(SkillNamespace ns, const std::string& component, SkillManifest& manifest) const;
    int getPromptResolved(const std::string& qualifiedName, Prompt& out) const;
    int resolveName(const std::string& componentNs, const std::string& componentName,
                    const std::string& shortName, std::string& qualifiedOut) const;
    std::unordered_map<std::string, std::string> buildDispatchTable() const;
    std::vector<std::string> listSkills(std::optional<SkillNamespace> ns) const;
    int addTool(const std::string& component, const SkillTool& tool);
    int addPrompt(const std::string& component, const Prompt& prompt);
    int updateTool(const std::string& component, const std::string& name, const SkillTool& tool);
    int install(const std::string& sourceUrl, bool force = false);
    int install(const std::string& sourceUrl, const std::string& commit, bool force = false);
    int remove(const std::string& qualifiedName);
    int gc(bool dryRun = false);
    int validate(const std::string& qualifiedName, const std::string& commit, std::string& report);
};

} // namespace a0::skills
```

## 3. Architecture

```mermaid
graph TB
    subgraph Skills_SubModule
        SKILLS_H[skills.h]
        MGR[SkillManager]
        LOADER[SkillLoader]
        VM[VersionManager]
        VE[ValidationEngine]
    end

    SKILLS_H --> MGR
    MGR --> LOADER
    MGR --> VM
    MGR --> VE
    MGR --> AGENT_CORE[AgentCore]
    MGR --> SKILL_RUNNER[SkillRunner]
