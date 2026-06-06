# SkillManager Spec

## 1. Overview

Public facade for the Skills sub-module. Manages the three-tier namespace (system/local/github), loads skill manifests from disk, resolves qualified names for tools and prompts, and provides lifecycle operations (install, remove, gc, validate). Holds the `m_handlers` registry for C++ system tool dispatch and the `m_toolState` per-session state bag.

**Source files:** `src/skills/skill_manager.cpp`

**Dependencies:** `SkillLoader`, `VersionManager`, `ValidationEngine`, `CommandRunner`, `ToolRunner`, `DockerToolRunner`

## 2. Component Specifications

```cpp
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
};
```

## 3. Testing Requirements

| Method | Test Case | Expected |
|--------|-----------|----------|
| `registerHandler` | New handler | Stored in m_handlers, executable |
| `executeTool` | Exact match | Handler output returned |
| `executeTool` | 2-part alias | `system_bash` → `system_bash_bash` handler |
| `executeTool` | Wildcard | `system_git_status` → xGitCommand via `HandlerContext::subcommand` |
| `executeTool` | System tool with no handler | Error string |
| `executeTool` | Command tool with runners | Subprocess output returned |
| `executeTool` | Command tool without runners | Error "no ToolRunner available" |
| `executeToolStreaming` | System handler | Sync fallback, single onChunk call |
| `executeToolStreaming` | Command tool | Delegates to runner |
| `schemas` | defaultOnly=true | Only tools with `default_=true` and parameters |
| `schemas` | defaultOnly=false | All tools with parameters |
| `missingHandlers` | All registered | Empty vector |
| `missingHandlers` | Unregistered systemTool | Vector with that tool's qualified name |
| `setRecordingSession` | Active | Tool results auto-recorded to persistence |
| `toolState` accessor | Returns reference | `m_toolState` accessible |
