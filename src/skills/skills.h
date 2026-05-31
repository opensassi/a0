#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>
#include <ctime>
#include "nlohmann/json.hpp"
#include "../agent_interfaces.h"

namespace a0::persistence { class PersistenceStore; }

namespace a0::skills {

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
    int timeoutSecs = 30;
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

/// Parse "github_alice:utils:list_files" → ns="github_alice", component="utils", tool="list_files"
/// Parse "system:bash" → ns="system", component="bash", tool="bash"
/// Parse "local:my_comp" → ns="local", component="my_comp", tool=""
bool parseQualifiedName(const std::string& qualified,
                        std::string& ns,
                        std::string& component,
                        std::string& name);

/// Build "local:file_utils:list_files" from parts.
std::string buildQualifiedName(const std::string& ns,
                                const std::string& component,
                                const std::string& name);

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

class SkillLoader;
class VersionManager;
class ValidationEngine;

// ---------------------------------------------------------------------------
// SkillManager — public facade
// ---------------------------------------------------------------------------

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

    // Resolve a prompt, flattening its chain into a single concatenated prompt.
    // out.prompt = chain[0].prompt + "\n\n" + chain[1].prompt + "\n\n" + target.prompt
    int getPromptResolved(const std::string& qualifiedName, Prompt& out) const;

    // Resolve a short name within a component:
    //   If shortName contains ':', do qualified lookup directly
    //   Otherwise, try <componentNs:componentName:shortName>, then qualified lookup
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

private:
    std::string m_skillsRoot;
    std::string m_storeRoot;
    SkillLoader* m_loader;
    VersionManager* m_versionMgr;
    ValidationEngine* m_validator;

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
