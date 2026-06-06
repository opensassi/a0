# Skills Header Spec

## 1. Overview

Central header for the Skills sub-module. Defines data structures (`SkillTool`, `SkillManifest`, `HandlerContext`, `StoredVersion`, `InvocationRecord`), qualified name helpers, and the `SkillManager` facade class. `SkillManager` is the unified dispatch layer for all tools — both C++ system tool handlers and command-based tools executed via `ToolRunner`/`DockerToolRunner`.

**Source files:** `src/skills/skills.h`, `src/skills/skill_manager.cpp`, `src/skills/CMakeLists.txt`

**Dependencies:** `agent_interfaces.h`, `handler_results.h`, `tool_state.h`, nlohmann/json

## 2. Data Structures

```cpp
#include "../tool_state.h"

namespace a0 { class DockerSecurityFilter; }
namespace a0::persistence { class PersistenceStore; }
class ToolRunner;
class DockerToolRunner;

namespace a0::skills {

enum class SkillNamespace { SYSTEM, LOCAL, GITHUB };

struct ToolSchema { nlohmann::json input; nlohmann::json output; };

struct HandlerContext {
    std::string subcommand;   // wildcard suffix or tool name
    ToolState* toolState = nullptr;
};

struct SkillTool {
    std::string name, description, command, inputMode = "stdin";
    ToolSchema schema;
    std::string dockerImage;
    TrustLevel trustLevel = TrustLevel::MEDIUM;
    std::vector<std::string> aptDependencies;
    bool systemTool = false;
    bool default_ = false;
    int timeoutSecs = 30;
    nlohmann::json parameters;
    std::string subCommand;
    bool streaming = false;
};

using ToolHandler = std::function<::a0::HandlerResult(const nlohmann::json& params,
                                                       const HandlerContext& ctx)>;

struct CompatBridge { std::string toolName, since, bridgeCommand, description; };

struct SkillManifest {
    std::string name, version, description;
    SkillNamespace ns;
    std::string sourceUrl, commitHash;
    std::vector<SkillTool> tools;
    std::vector<Prompt> prompts;
    std::vector<CompatBridge> compat;
    std::unordered_map<std::string, std::string> dependencies;
    std::vector<std::string> subModules;
};

struct StoredVersion { std::string commitHash, version; int refcount; time_t installedAt; };
struct InvocationRecord { std::string toolName; nlohmann::json params, output; int64_t timestamp; };

bool parseQualifiedName(const std::string& qualified,
                         std::string& ns, std::string& component, std::string& name);
std::string buildQualifiedName(const std::string& ns,
                                const std::string& component, const std::string& name);

class SkillLoader;
class VersionManager;
class ValidationEngine;

class SkillManager {
public:
    SkillManager(const std::string& skillsRoot,
                 const std::string& storeRoot,
                 ::a0::persistence::PersistenceStore* persistence = nullptr);
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

    void registerHandler(const std::string& qualifiedName, ToolHandler handler);
    json executeTool(const std::string& qualifiedName, const json& params);
    ::a0::HandlerResult executeToolWithMeta(const std::string& qualifiedName, const json& params,
        int* seq = nullptr, const std::string& toolCallId = "", int64_t subSessionId = 0);
    a0::StreamHandle executeToolStreaming(const std::string& qualifiedName,
        const json& params, a0::StreamCallback onChunk,
        int* seq = nullptr, const std::string& toolCallId = "", int64_t subSessionId = 0);
    std::vector<::ToolSchema> schemas(bool defaultOnly = true) const;
    std::vector<std::string> missingHandlers() const;
    void setRecordingSession(int64_t sessionDbId);
    void setToolRunner(::ToolRunner* runner);
    void setDockerRunner(::DockerToolRunner* runner);
    void setDockerSecurityFilter(::a0::DockerSecurityFilter* filter);
    ToolState& toolState() { return m_toolState; }

private:
    std::string m_skillsRoot, m_storeRoot;
    SkillLoader* m_loader;
    VersionManager* m_versionMgr;
    ValidationEngine* m_validator;
    std::unordered_map<std::string, ToolHandler> m_handlers;
    ::ToolRunner* m_toolRunner = nullptr;
    ::DockerToolRunner* m_dockerRunner = nullptr;
    ::a0::DockerSecurityFilter* m_dockerSecurityFilter = nullptr;
    ::a0::persistence::PersistenceStore* m_persistence = nullptr;
    int64_t m_sessionDbId = 0;
    ToolState m_toolState;

    int xEnsureNs(const std::string& ns, SkillNamespace& outNs) const;
    int xInstallFromGit(const std::string& url, const std::string& commit,
                         bool force, SkillNamespace ns, SkillManifest& manifest);
};

} // namespace a0::skills
```

## 3. Resolution Order

`executeToolWithMeta(qn, params)` follows:

1. **Exact match** — `m_handlers.find(qn)` — direct handler lookup
2. **2-part alias** — For `ns_comp`, try `ns_comp_comp` (tool name == component name)
3. **Wildcard** — `ns_comp_*` — handler receives `ctx.subcommand` set to the tool name after the last `_`
4. **System tool with no handler** — Error returned if `systemTool==true` but no handler registered
5. **Command tool** — Execute via `ToolRunner`/`DockerToolRunner` if runners are set
6. **Error** — Returns error string

### Streaming Resolution Order

| Step | Lookup | Behaviour |
|------|--------|-----------|
| 1 | Exact handler match | Run handler synchronously, onChunk once, return completed handle |
| 2 | Wildcard match | Same as exact |
| 3 | System tool (no streaming) | Error message (system tools do not stream) |
| 4 | Command tool via ToolRunner | Delegates to `ToolRunner::runStreaming()` / `DockerToolRunner::runStreaming()` |
