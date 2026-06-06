#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <ctime>
#include "nlohmann/json.hpp"
#include "../agent_interfaces.h"
#include "../handler_results.h"
#include "../tool_state.h"

class ToolState;

namespace a0 { class DockerSecurityFilter; }
namespace a0::persistence { class PersistenceStore; }

// Forward decls for types in global scope
class ToolRunner;
class DockerToolRunner;

namespace a0::skills {

// ---------------------------------------------------------------------------
// HandlerContext — contextual info passed to every system tool handler
// ---------------------------------------------------------------------------

struct HandlerContext {
    std::string subcommand;   // wildcard suffix or tool name
    ToolState* toolState = nullptr;   // per-session shared state (nullable)
};

// ---------------------------------------------------------------------------
// Tool handler — C++ function that implements a system tool
// ---------------------------------------------------------------------------

using ToolHandler = std::function<::a0::HandlerResult(
    const nlohmann::json& params,
    const HandlerContext& ctx)>;

// ---------------------------------------------------------------------------

/// Namespace identifier for a skill source.
enum class SkillNamespace {
    SYSTEM,   // skills/system/ — shipped with agent, read-only, not overridable
    LOCAL,    // skills/local/  — agent-created, writable
    GITHUB    // skills/github_<user>/ — installed from GitHub, read-only
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

struct ToolSchema {
    nlohmann::json input;
    nlohmann::json output;
};

struct SkillTool {
    std::string name;
    std::string description;
    std::string command;
    std::string inputMode = "stdin";
    ToolSchema schema;
    std::string dockerImage;
    TrustLevel trustLevel = TrustLevel::MEDIUM;
    std::vector<std::string> aptDependencies;
    bool systemTool = false;
    bool default_ = false;
    int timeoutSecs = 30;
    nlohmann::json parameters;   // JSON Schema for LLM function calling
    std::string subCommand;      // override CLI subcommand (e.g. "rev-parse" for tool "rev_parse")
    bool streaming = false;      // tool supports streaming output
};

struct CompatBridge {
    std::string toolName;
    std::string since;
    std::string bridgeCommand;
    std::string description;
};

struct SkillManifest {
    std::string name;
    std::string version;
    std::string description;
    SkillNamespace ns;
    std::string sourceUrl;
    std::string commitHash;
    std::vector<SkillTool> tools;
    std::vector<Prompt> prompts;
    std::vector<CompatBridge> compat;
    std::unordered_map<std::string, std::string> dependencies;
    std::vector<std::string> subModules;   // sub-component directories to auto-load
};

struct StoredVersion {
    std::string commitHash;
    std::string version;
    int refcount = 0;
    time_t installedAt = 0;
};

struct InvocationRecord {
    std::string toolName;
    nlohmann::json params;
    nlohmann::json output;
    int64_t timestamp = 0;
};

// ---------------------------------------------------------------------------
// Qualified name helpers
// ---------------------------------------------------------------------------

/// Parse "system_task_manager_add_task" → ns="system", component="task_manager", name="add_task"
/// Parse "system_bash" → ns="system", component="bash", name="bash"
/// First segment is ns, last segment is name, everything between is component.
/// For 2 segments (ns_name), component = name.
bool parseQualifiedName(const std::string& qualified,
                        std::string& ns,
                        std::string& component,
                        std::string& name);

/// Build "system_task_manager_add_task" from parts.
std::string buildQualifiedName(const std::string& ns,
                                const std::string& component,
                                const std::string& name);

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

class SkillLoader;
class VersionManager;
class ValidationEngine;
class DockerSecurityFilter;

// ---------------------------------------------------------------------------
// SkillManager — public facade
// ---------------------------------------------------------------------------

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

    // Resolve a prompt, flattening its chain into a single concatenated prompt.
    // out.prompt = chain[0].prompt + "\n\n" + chain[1].prompt + "\n\n" + target.prompt
    int getPromptResolved(const std::string& qualifiedName, Prompt& out) const;

    // Resolve a short name within a component:
    //   If shortName contains '-', do qualified lookup directly
    //   Otherwise, try <componentNs-componentName-shortName>
    int resolveName(const std::string& componentNs,
                    const std::string& componentName,
                    const std::string& shortName,
                    std::string& qualifiedOut) const;

    // Build dispatch table: LLM-facing short name → qualified internal name.
    // Collision resolution: if two entries share the same last segment, prepend
    // component name, then namespace:component, then full underscores.
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

    // --- Handler registry (C++ system tool dispatch) ---

    /// Register a C++ handler function for a system tool.
    /// For wildcard handlers (e.g. "system-git-*"), the function receives
    /// ctx.subcommand set to the tool name after the last dash.
    void registerHandler(const std::string& qualifiedName, ToolHandler handler);

    /// Execute a tool by qualified name. Resolution order:
    ///   1. Exact match in registered C++ handlers
    ///   2. Wildcard match (ns-component-*) with ctx.subcommand set
    ///   3. If tool has command field, run via SubprocessToolRunner/DockerToolRunner
    ///   4. Error if tool not found or unhandled
    json executeTool(const std::string& qualifiedName, const json& params);

    /// Execute a tool with streaming output.
    /// For command tools with streaming=true, delegates to ToolRunner::runStreaming.
    /// For system tools, falls through to the synchronous executeToolWithMeta path.
    /// \param qualifiedName  Fully qualified tool name.
    /// \param params         Tool parameters.
    /// \param onChunk        Called from a background thread with (data, direction) chunks.
    /// \param seq            Optional sequence counter for persistence recording.
    /// \param toolCallId     LLM tool call id.
    /// \param subSessionId   Fork sub-session id.
    a0::StreamHandle executeToolStreaming(const std::string& qualifiedName,
        const json& params, a0::StreamCallback onChunk,
        int* seq = nullptr, const std::string& toolCallId = "",
        int64_t subSessionId = 0);

    /// Enable auto-recording of tool execution results to persistence.
    /// When active, every executeToolWithMeta call records appendMessage + appendInvocation.
    void setRecordingSession(int64_t sessionDbId);

    /// Full result with recommendedTools (for tools_for_prompt).
    /// When recording is active and seq is non-null, auto-records tool result to persistence.
    /// \param seq         Sequence counter (incremented on record). Pass nullptr to skip recording.
    /// \param toolCallId  LLM tool call id (empty for non-LLM calls).
    /// \param subSessionId Fork sub-session id (0 for main session).
    ::a0::HandlerResult executeToolWithMeta(const std::string& qualifiedName, const json& params,
        int* seq = nullptr, const std::string& toolCallId = "", int64_t subSessionId = 0);

    /// Build LLM tool schemas from loaded manifests.
    /// When defaultOnly = true, only includes tools with default_ = true.
    std::vector<::ToolSchema> schemas(bool defaultOnly = true) const;

    /// Returns all systemTool qualified names that have no registered
    /// C++ handler (exact, wildcard, or 2-part alias). Empty vector = all good.
    std::vector<std::string> missingHandlers() const;

    /// Set runners for non-system tool execution (command-based tools).
    void setToolRunner(::ToolRunner* runner);
    void setDockerRunner(::DockerToolRunner* runner);
    void setDockerSecurityFilter(::a0::DockerSecurityFilter* filter);

    ToolState& toolState() { return m_toolState; }

private:
    std::string m_skillsRoot;
    std::string m_storeRoot;
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

    SkillManager(const SkillManager&) = delete;
    SkillManager& operator=(const SkillManager&) = delete;

    int xEnsureNs(const std::string& ns, SkillNamespace& outNs) const;
    int xInstallFromGit(const std::string& url,
                         const std::string& commit,
                         bool force,
                         SkillNamespace ns,
                         SkillManifest& manifest);
};

} // namespace a0::skills
