# Skills Header Spec

## 1. Overview

Central header for the Skills sub-module. Defines data structures (SkillTool, SkillManifest, StoredVersion, InvocationRecord), qualified name helpers, and the `SkillManager` facade class. All skills source files include this header.

**Source files:** `src/skills/skills.h`, `src/skills/skill_manager.cpp`, `src/skills/CMakeLists.txt`

**Dependencies:** `agent_interfaces.h`, nlohmann/json

## 2. Data Structures

```cpp
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

class SkillManager { /* facade — see skill_manager.spec.md */ };

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
