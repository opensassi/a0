#include "system_tools/registry.h"
#include "agent_interfaces.h"
#include "skills/skills.h"
#include "docker_security_filter.h"
#include <sstream>
#include <algorithm>

namespace a0 {

// ---------------------------------------------------------------------------
// parseToolPath: "/system/git/commit" → {ns="system", component="git", tool="commit"}
// ---------------------------------------------------------------------------

ParsedToolPath SystemToolRegistry::parseToolPath(const std::string& path) {
    ParsedToolPath result;
    std::string rest = path;
    if (rest.size() > 1 && rest.front() == '/') rest = rest.substr(1);
    size_t firstSlash = rest.find('/');
    if (firstSlash == std::string::npos) {
        result.ns = rest;
        result.component = rest;
        result.tool = rest;
        return result;
    }
    result.ns = rest.substr(0, firstSlash);
    std::string remainder = rest.substr(firstSlash + 1);
    size_t secondSlash = remainder.find('/');
    if (secondSlash == std::string::npos) {
        result.component = remainder;
        result.tool = remainder;
    } else {
        result.component = remainder.substr(0, secondSlash);
        result.tool = remainder.substr(secondSlash + 1);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Constructor — register handler groups by component name
// ---------------------------------------------------------------------------

SystemToolRegistry::SystemToolRegistry() {
    m_handlerGroups["bash"] = [](const std::string&, const json& p) { return xBash(p); };
    m_handlerGroups["read"] = [](const std::string&, const json& p) { return xRead(p); };
    m_handlerGroups["glob"] = [](const std::string&, const json& p) { return xGlob(p); };
    m_handlerGroups["grep"] = [](const std::string&, const json& p) { return xGrep(p); };
    m_handlerGroups["edit"] = [](const std::string&, const json& p) { return xEdit(p); };
    m_handlerGroups["write"] = [](const std::string&, const json& p) { return xWrite(p); };

    m_handlerGroups["show_skills"] = [this](const std::string&, const json& p) { return xShowSkills(p); };
    m_handlerGroups["show_skill_tools"] = [this](const std::string&, const json& p) { return xShowSkillTools(p); };
    m_handlerGroups["tools_for_prompt"] = [this](const std::string&, const json& p) { return xToolsForPrompt(p); };

    m_handlerGroups["git"] = [](const std::string& tool, const json& p) { return xGitCommand(tool, p); };
    m_handlerGroups["docker"] = [this](const std::string& tool, const json& p) { return xDockerCommand(tool, p); };
    m_handlerGroups["docker_compose"] = [this](const std::string& tool, const json& p) { return xDockerComposeCommand(tool, p); };
}

// ---------------------------------------------------------------------------
// isSystemTool: true if path starts with "/system/" or is a core tool name
// ---------------------------------------------------------------------------

bool SystemToolRegistry::isSystemTool(const std::string& path) {
    if (path.rfind("/system/", 0) == 0 || path == "/system") return true;
    if (path.rfind("system_", 0) == 0) return true;
    // Accept short names for core tools (backward compat)
    static const std::vector<std::string> coreTools = {
        "bash", "read", "glob", "grep", "edit", "write",
        "show_skills", "show_skill_tools", "tools_for_prompt"
    };
    for (const auto& t : coreTools) {
        if (path == t) return true;
    }
    return false;
}

bool SystemToolRegistry::xIsDockerCommand(const std::string& command) {
    std::string c = command;
    // Trim leading whitespace
    size_t start = 0;
    while (start < c.size() && (c[start] == ' ' || c[start] == '\t')) ++start;
    c = c.substr(start);
    if (c == "docker" || c.rfind("docker ", 0) == 0 || c.rfind("docker\t", 0) == 0) return true;
    if (c == "docker-compose" || c.rfind("docker-compose ", 0) == 0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// listTools
// ---------------------------------------------------------------------------

std::vector<std::string> SystemToolRegistry::listTools() const {
    std::vector<std::string> names;
    for (const auto& [component, _] : m_handlerGroups) {
        names.push_back(component);
    }
    return names;
}

// ---------------------------------------------------------------------------
// execute
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::execute(const std::string& toolPath, const json& params) {
    // Normalize: strip /system/ or system_ prefix
    std::string name = toolPath;
    if (name.rfind("/system/", 0) == 0) name = name.substr(8);
    else if (name.rfind("system_", 0) == 0) name = name.substr(7);

    // Handle git_* tools: system_git_status → handler group "git" with tool "status"
    if (name.rfind("git_", 0) == 0) {
        auto it = m_handlerGroups.find("git");
        if (it != m_handlerGroups.end()) return it->second(name.substr(4), params);
    }

    // Handle docker_compose_* tools: system_docker_compose_up → handler "docker_compose" tool "up"
    if (name.rfind("docker_compose_", 0) == 0) {
        auto it = m_handlerGroups.find("docker_compose");
        if (it != m_handlerGroups.end()) return it->second(name.substr(15), params);
    }

    // Handle docker_* tools: system_docker_run → handler "docker" tool "run"
    if (name.rfind("docker_", 0) == 0) {
        auto it = m_handlerGroups.find("docker");
        if (it != m_handlerGroups.end()) return it->second(name.substr(7), params);
    }

    // Handle fs_* tools: system_fs_read → handler group "read"
    if (name.rfind("fs_", 0) == 0) {
        auto it = m_handlerGroups.find(name.substr(3));
        if (it != m_handlerGroups.end()) return it->second(name.substr(3), params);
    }

    // Direct handler group lookup: bash, read, show_skills, etc.
    auto it = m_handlerGroups.find(name);
    if (it != m_handlerGroups.end()) {
        return it->second(name, params);
    }

    return {"ERROR: unknown system tool: " + toolPath};
}

// ---------------------------------------------------------------------------
// schemas — returns 9 default tools for the LLM function calling schema
// ---------------------------------------------------------------------------

static json xParam(const std::string& type, const std::string& desc,
                    bool required, const json& defaultVal) {
    json p = {{"type", type}, {"description", desc}};
    if (!defaultVal.is_null()) p["default"] = defaultVal;
    return p;
}

std::vector<ToolSchema> SystemToolRegistry::schemas() const {
    std::vector<ToolSchema> result;

    // bash — the only general-purpose execution tool
    result.push_back({
        "bash",
        "Executes a given bash command. IMPORTANT: git and docker commands are rejected — use the appropriate system tool (e.g. git_*, docker_*) instead.",
        {{"type", "object"},
         {"properties", {
             {"command", xParam("string", "The command to execute", true, {})},
             {"description", xParam("string", "Clear, concise description (5-10 words)", true, {})},
             {"timeout", xParam("number", "Optional timeout in milliseconds", false, 120000)},
             {"workdir", xParam("string", "Working directory to run in", false, {})}
         }},
         {"required", {"command", "description"}}}
    });

    // read
    result.push_back({
        "read",
        "Read files or directories from the local filesystem",
        {{"type", "object"},
         {"properties", {
             {"file_path", xParam("string", "The absolute path to the file or directory to read", true, {})},
             {"offset", xParam("number", "The line number to start reading from (1-indexed)", false, 1)},
             {"limit", xParam("number", "Maximum number of lines to read", false, 2000)}
         }},
         {"required", {"file_path"}}}
    });

    // glob
    result.push_back({
        "glob",
        "Fast file pattern matching tool",
        {{"type", "object"},
         {"properties", {
             {"pattern", xParam("string", "Glob pattern (e.g. **/*.js)", true, {})},
             {"path", xParam("string", "Directory to search in", false, {})}
         }},
         {"required", {"pattern"}}}
    });

    // grep
    result.push_back({
        "grep",
        "Fast content search using regular expressions",
        {{"type", "object"},
         {"properties", {
             {"pattern", xParam("string", "The regex pattern to search for", true, {})},
             {"path", xParam("string", "Directory to search in", false, {})},
             {"include", xParam("string", "File pattern (e.g. *.cpp, *.{ts,tsx})", false, {})}
         }},
         {"required", {"pattern"}}}
    });

    // edit
    result.push_back({
        "edit",
        "Performs exact string replacements in files",
        {{"type", "object"},
         {"properties", {
             {"file_path", xParam("string", "The absolute path to the file to modify", true, {})},
             {"old_string", xParam("string", "The text to replace", true, {})},
             {"new_string", xParam("string", "The text to replace it with", true, {})},
             {"replace_all", xParam("boolean", "Replace all occurrences (default false)", false, false)}
         }},
         {"required", {"file_path", "old_string", "new_string"}}}
    });

    // write
    result.push_back({
        "write",
        "Writes content to a file on the local filesystem",
        {{"type", "object"},
         {"properties", {
             {"file_path", xParam("string", "The absolute path to the file to write", true, {})},
             {"content", xParam("string", "The content to write to the file", true, {})}
         }},
         {"required", {"file_path", "content"}}}
    });

    // show_skills
    result.push_back({
         "show_skills",
         "Browse the skill tree by path. Lists available skills with descriptions.",
         {{"type", "object"},
          {"properties", {
              {"path", xParam("string", "Skill tree path (e.g. /system/git)", false, "/")}
          }},
          {"required", json::array()}}
    });

    // show_skill_tools
    result.push_back({
        "show_skill_tools",
        "Browse available tools by category. Lists tool names, descriptions, and parameters.",
        {{"type", "object"},
         {"properties", {
             {"path", xParam("string", "Tool tree path (e.g. /git/porcelain)", false, "/")}
         }},
         {"required", json::array()}}
    });

    // tools_for_prompt
    result.push_back({
        "tools_for_prompt",
        "Analyze user intent and recommend skills/tools for a given prompt.",
        {{"type", "object"},
         {"properties", {
             {"prompt", xParam("string", "The user's prompt to analyze", true, {})}
         }},
         {"required", {"prompt"}}}
    });

    return result;
}

// ---------------------------------------------------------------------------
// getSchema — return schema for a specific tool path
// ---------------------------------------------------------------------------

ToolSchema SystemToolRegistry::getSchema(const std::string& path) const {
    auto all = schemas();
    for (const auto& s : all) {
        if (s.name == path) return s;
    }
    return {path, "", json::object()};
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void SystemToolRegistry::setInferenceProvider(InferenceProvider* provider) {
    m_inferenceProvider = provider;
}

void SystemToolRegistry::setSkillManager(a0::skills::SkillManager* mgr) {
    m_skillManager = mgr;
}

void SystemToolRegistry::setSkillRunner(SkillRunner* runner) {
    m_skillRunner = runner;
}

void SystemToolRegistry::setDockerSecurityFilter(DockerSecurityFilter* filter) {
    m_dockerSecurityFilter = filter;
}

} // namespace a0
