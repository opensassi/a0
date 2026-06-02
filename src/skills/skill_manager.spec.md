# SkillManager Spec

## 1. Overview

Public facade for the Skills sub-module. Manages the three-tier namespace (system/local/github), loads skill manifests from disk, resolves qualified names for tools and prompts, and provides lifecycle operations (install, remove, gc, validate).

SkillManager is the unified tool dispatch layer. It holds a registry of C++ handler functions (`m_handlers`) and handles both system tool dispatch (C++ calls) and command tool execution (subprocess via ToolRunner/DockerToolRunner).

**Source files:** `src/skills/skill_manager.cpp`

**Dependencies:** `SkillLoader`, `VersionManager`, `ValidationEngine`, `CommandRunner`, `ToolRunner`, `DockerToolRunner`

## 2. Component Specifications

```cpp
class SkillManager {
public:
    SkillManager(const std::string& skillsRoot,
                 const std::string& storeRoot,
                 a0::persistence::PersistenceStore* persistence = nullptr);
    ~SkillManager();

    // --- Existing lifecycle ---
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

    // --- Handler registry (unified tool dispatch) ---
    void registerHandler(const std::string& qualifiedName, ToolHandler handler);
    json executeTool(const std::string& qualifiedName, const json& params);
    HandlerResult executeToolWithMeta(const std::string& qualifiedName, const json& params);
    std::vector<ToolSchema> schemas(bool defaultOnly = true) const;
    std::vector<std::string> missingHandlers() const;

    // --- Runner wiring ---
    void setToolRunner(ToolRunner* runner);
    void setDockerRunner(DockerToolRunner* runner);
    void setDockerSecurityFilter(DockerSecurityFilter* filter);
};
```

## 3. Handler Dispatch

```cpp
HandlerResult SkillManager::executeToolWithMeta(const std::string& qn, const json& params) {
    // 1. Exact match in m_handlers
    auto it = m_handlers.find(qn);
    if (it != m_handlers.end()) return it->second(params);

    // 2. 2-part alias: "system:bash" → "system:bash:bash"
    //    (when parseQualifiedName gives name == component)

    // 3. Wildcard: "system:git:commit" → "system:git:*"
    //    Sets params["_subcommand"] = "commit"

    // 4. Command tool via ToolRunner (systemTool == false, has command field)

    // 5. Error
}
```

### Resolution Order

| Step | Lookup | Example | When |
|------|--------|---------|------|
| 1 | Exact match | `system:fs:read` | Handler registered with 3-part key |
| 2 | 2-part alias | `system:bash` → `system:bash:bash` | Tool name equals component name |
| 3 | Wildcard | `system:git:commit` → `system:git:*` | Handler registered with wildcard key |
| 4 | Subprocess | Tool with `command` field | `systemTool==false` + runners available |
| 5 | Error | — | System tool with no handler |

## 4. Schema Generation

```cpp
std::vector<ToolSchema> SkillManager::schemas(bool defaultOnly = true) const {
    // Iterates all loaded manifests (system/local/github)
    // For each tool:
    //   - If defaultOnly && !tool.default_ → skip
    //   - If parameters is null/empty → skip
    //   - Builds ToolSchema{name, description, parameters}
}
```

The default tools set (bash, read, edit, write, glob, grep, show_skills, show_skill_tools, tools_for_prompt) is defined by `"default": true` in their respective `skill.json` files, not hardcoded in C++.

## 5. Startup Validation

```cpp
std::vector<std::string> SkillManager::missingHandlers() const {
    // Iterates all loaded manifests
    // For each tool with systemTool==true:
    //   - Check exact match in m_handlers
    //   - Check wildcard (ns:comp:*)
    //   - Check 2-part alias (if name == comp)
    //   - If none match → add to missing list
}
```

Called in `AgentCore::init()` after `loadAll()`. If any system tool is missing a handler, the agent exits with a fatal error listing every missing tool.

## 6. Architecture

```mermaid
graph TB
    SM[SkillManager]
    SL[SkillLoader]
    VM[VersionManager]
    VE[ValidationEngine]
    CR[CommandRunner]
    HANDLERS[m_handlers]
    TR[ToolRunner]
    DTR[DockerToolRunner]

    SM --> SL
    SM --> VM
    SM --> VE
    SM --> HANDLERS
    SM --> TR
    SM --> DTR
    VE --> CR

    subgraph Handler_Registration
        CORE[system_handlers.cpp: xBash, xRead, ...]
        GIT[xGitCommand via system:git:*]
        DOCKER[xDockerCommand via system:docker:*]
        META[xShowSkills, xToolsForPrompt]
    end

    HANDLERS --> CORE
    HANDLERS --> GIT
    HANDLERS --> DOCKER
    HANDLERS --> META

    subgraph Namespace_Tiers
        SYSTEM[skills/system/]
        LOCAL[skills/local/]
        GITHUB[skills/github_*/]
    end

    SL --> SYSTEM
    SL --> LOCAL
    SL --> GITHUB
    VM --> STORE[.a0/store/]
```

## 7. Testing Requirements

| Method | Test Case | Expected |
|--------|-----------|----------|
| `registerHandler` | New handler | Stored in m_handlers, executable |
| `executeTool` | Exact match | Handler output returned |
| `executeTool` | 2-part alias | `system:bash` returns bash handler output |
| `executeTool` | Wildcard | `system:git:status` runs xGitCommand("status") |
| `executeTool` | Unknown system tool | Error string with tool name |
| `executeTool` | Command tool with runners | Runs via ToolRunner, returns output |
| `executeTool` | Command tool without runners | Error "no ToolRunner available" |
| `executeToolWithMeta` | tools_for_prompt | HandlerResult with recommendedTools |
| `schemas` | defaultOnly=true | Only tools with `default_=true` and parameters |
| `schemas` | defaultOnly=false | All tools with parameters |
| `missingHandlers` | All registered | Empty vector |
| `missingHandlers` | One missing | Vector with that tool's qualified name |
| `missingHandlers` | After wildcard registration | Empty (git wildcard covers all git_* tools) |
