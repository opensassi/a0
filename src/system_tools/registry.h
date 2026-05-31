#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ToolSchema;
class InferenceProvider;
class SkillRunner;

namespace a0::skills { class SkillManager; }

namespace a0 {

struct SystemToolResult {
    std::string output;
    std::vector<std::string> recommendedTools;
};

struct ParsedToolPath {
    std::string ns;        // "system", "local"
    std::string component; // "git", "bash"
    std::string tool;      // "commit", "bash"
};

class SystemToolRegistry {
public:
    SystemToolRegistry();

    SystemToolResult execute(const std::string& toolPath, const json& params);

    static bool isSystemTool(const std::string& path);

    std::vector<std::string> listTools() const;

    std::vector<ToolSchema> schemas() const;

    ToolSchema getSchema(const std::string& path) const;

    void setInferenceProvider(class InferenceProvider* provider);
    void setSkillManager(class a0::skills::SkillManager* mgr);
    void setSkillRunner(class SkillRunner* runner);
    void setDockerSecurityFilter(class DockerSecurityFilter* filter);

    static ParsedToolPath parseToolPath(const std::string& path);

private:
    using HandlerGroup = std::function<SystemToolResult(
        const std::string& tool, const json& params)>;

    static SystemToolResult xBash(const json& params);
    static SystemToolResult xRead(const json& params);
    static SystemToolResult xGlob(const json& params);
    static SystemToolResult xGrep(const json& params);
    static SystemToolResult xEdit(const json& params);
    static SystemToolResult xWrite(const json& params);

    SystemToolResult xShowSkills(const json& params);
    SystemToolResult xShowSkillTools(const json& params);
    SystemToolResult xToolsForPrompt(const json& params);

    static SystemToolResult xGitCommand(const std::string& subcommand, const json& params);
    SystemToolResult xDockerCommand(const std::string& subcommand, const json& params);
    SystemToolResult xDockerComposeCommand(const std::string& subcommand, const json& params);

    static bool xIsGitCommand(const std::string& command);
    static bool xIsDockerCommand(const std::string& command);

    InferenceProvider* m_inferenceProvider = nullptr;
    a0::skills::SkillManager* m_skillManager = nullptr;
    SkillRunner* m_skillRunner = nullptr;

    std::unordered_map<std::string, HandlerGroup> m_handlerGroups;
    mutable json m_cachedToolTree;
    class DockerSecurityFilter* m_dockerSecurityFilter = nullptr;
};

} // namespace a0
