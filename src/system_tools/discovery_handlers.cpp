#include "system_tools/registry.h"
#include "agent_interfaces.h"
#include "skills/skills.h"
#include <sstream>
#include <algorithm>
#include <regex>
#include <unordered_set>

namespace a0 {

// ---------------------------------------------------------------------------
// show_skills — navigate the skill tree
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xShowSkills(const json& params) {
    auto pathIt = params.find("path");
    std::string path = (pathIt != params.end() && pathIt->is_string())
        ? pathIt->get<std::string>() : "/";

    std::ostringstream out;
    out << "Path: " << path << "\n";

    if (path == "/" || path.empty()) {
        out << "Available namespaces: system, local\n";
        out << "  system/ — built-in skills\n";
        out << "  local/ — user-created skills\n";
        out << "\nUse show_skills('/system') or show_skills('/local') to browse.";
        return {out.str()};
    }

    std::string rest = path;
    if (rest.front() == '/') rest = rest.substr(1);
    size_t firstSlash = rest.find('/');
    std::string ns = (firstSlash == std::string::npos) ? rest : rest.substr(0, firstSlash);

    if (m_skillManager) {
        auto nsEnum = (ns == "system") ? a0::skills::SkillNamespace::SYSTEM
                    : (ns == "local")  ? a0::skills::SkillNamespace::LOCAL
                    : a0::skills::SkillNamespace::GITHUB;

        auto components = m_skillManager->listSkills(nsEnum);

        if (rest.find('/') == std::string::npos) {
            // Listing all components in a namespace
            out << "Namespace: " << ns << "\n";
            out << "Components:\n";
            for (const auto& comp : components) {
                out << "  /" << ns << "/" << comp << "/\n";
            }
            out << "\nUse show_skills('/" << ns << "/component') to browse a component's skills.";
            return {out.str()};
        }

        // Second level: list skills within a component
        std::string component = rest.substr(firstSlash + 1);
        if (!component.empty() && component.back() == '/') component.pop_back();

        // Try known prompt names for this component
        static const char* knownPrompts[] = {
            "start_session", "finish_session", "sync",
            "system_design_base", "generate", "create_issue"
        };
        out << "Skills in /" << ns << "/" << component << ":\n";
        bool found = false;
        for (const auto& pn : knownPrompts) {
            Prompt resolved;
            std::string qn = ns + ":" + component + ":" + pn;
            if (m_skillManager->getPromptResolved(qn, resolved) == 0) {
                out << "  " << pn << " — " << resolved.description << "\n";
                found = true;
            }
        }
        if (!found) {
            out << "  (no skills found)\n";
            out << "\nAvailable components in " << ns << ":\n";
            for (const auto& comp : components) {
                out << "  /" << ns << "/" << comp << "/\n";
            }
        }
    } else {
        out << "(no skill manager)\n";
    }

    out << "\nSkills are invoked by their short name as tool calls (e.g. create_local_cli_skill, start_session).";
    return {out.str()};
}

// ---------------------------------------------------------------------------
// show_skill_tools — list tools (from skill.json manifests)
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xShowSkillTools(const json& params) {
    auto pathIt = params.find("path");
    std::string path = (pathIt != params.end() && pathIt->is_string())
        ? pathIt->get<std::string>() : "/";

    std::ostringstream out;
    out << "Path: " << path << "\n";

    if (path == "/" || path.empty()) {
        out << "System tool categories:\n";
        out << "  /system/ — core tools (bash, read, glob, grep, edit, write, etc.)\n";
        out << "  /git/ — git version control tools (120+ commands)\n";
        out << "\nUse show_skill_tools('/system') or show_skill_tools('/git') to browse.";
        return {out.str()};
    }

    std::string rest = path;
    if (rest.front() == '/') rest = rest.substr(1);
    std::string firstPart = rest;
    if (firstPart.find('/') != std::string::npos)
        firstPart = firstPart.substr(0, firstPart.find('/'));

    // Check SkillManager for tool descriptions
    if (m_skillManager) {
        a0::skills::SkillTool skillTool;
        std::string managerNs = "system";
        std::string managerComp = firstPart;

        // Try as a component (e.g., "git")
        auto components = m_skillManager->listSkills(std::nullopt);
        bool isComponent = false;
        for (const auto& c : components) {
            if (c == managerComp) { isComponent = true; break; }
        }

        if (isComponent) {
            if (rest == firstPart) {
                // List tools at component root
                out << "Tools in /" << firstPart << ":\n";
                // Load tool descriptions from SkillManager manifest
                a0::skills::SkillManifest manifest;
                for (const auto& ns : {a0::skills::SkillNamespace::SYSTEM,
                                        a0::skills::SkillNamespace::LOCAL}) {
                    if (m_skillManager->getManifest(ns, managerComp, manifest) == 0) {
                        for (const auto& t : manifest.tools) {
                            out << "  " << t.name;
                            if (!t.description.empty())
                                out << " — " << t.description.substr(0, 80);
                            out << "\n";
                        }
                        if (manifest.tools.empty())
                            out << "  (uses {{tool:...}} expansion in skill templates)\n";
                        break;
                    }
                }
                return {out.str()};
            }

            // Tool-specific view
            std::string toolName = rest.substr(firstPart.find('/') + 1);
            a0::skills::SkillManifest manifest;
            for (const auto& ns : {a0::skills::SkillNamespace::SYSTEM,
                                    a0::skills::SkillNamespace::LOCAL}) {
                if (m_skillManager->getManifest(ns, managerComp, manifest) == 0) {
                    for (const auto& t : manifest.tools) {
                        if (t.name == toolName) {
                            std::string nsName = (ns == a0::skills::SkillNamespace::SYSTEM) ? "system" : "local";
                            out << "Tool: " << toolName << "\n";
                            out << "Description: " << t.description << "\n";
                            out << "Path: /" << nsName << "/" << managerComp << "/" << toolName << "\n";
                            if (t.command.empty())
                                out << "Implementation: built-in (system tool handler)\n";
                            else
                                out << "Command: " << t.command << "\n";
                            return {out.str()};
                        }
                    }
                    // No specific match — show available tool names
                    out << "Tool '" << toolName << "' not found. Available tools in " << firstPart << ":\n";
                    for (const auto& t : manifest.tools) {
                        out << "  " << t.name << "\n";
                    }
                    return {out.str()};
                }
            }
        }
    }

    // Fallback
    if (firstPart == "git") {
        out << "Git tools: 120+ commands available via {{tool:commit ...}} in skill templates.\n";
        out << "Use git_start_session for the standard workflow.\n";
    } else if (firstPart == "system") {
        out << "Core system tools: bash, read, glob, grep, edit, write, show_skills, show_skill_tools, tools_for_prompt\n";
    }

    return {out.str()};
}

// ---------------------------------------------------------------------------
// tools_for_prompt — analyze user intent and recommend skills/tools
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xToolsForPrompt(const json& params) {
    auto promptIt = params.find("prompt");
    if (promptIt == params.end() || !promptIt->is_string()) {
        return {"ERROR: missing required string parameter 'prompt'", {}};
    }
    std::string promptText = promptIt->get<std::string>();

    // Build inventory of all available skills and tools from SkillManager
    std::ostringstream toolInv;
    if (m_skillManager) {
        for (const auto& ns : {a0::skills::SkillNamespace::SYSTEM,
                                a0::skills::SkillNamespace::LOCAL}) {
            auto components = m_skillManager->listSkills(ns);
            for (const auto& comp : components) {
                a0::skills::SkillManifest manifest;
                if (m_skillManager->getManifest(ns, comp, manifest) == 0) {
                    std::string nsName = (ns == a0::skills::SkillNamespace::SYSTEM) ? "system" : "local";

                    if (!manifest.prompts.empty()) {
                        toolInv << "Skills under /" << nsName << "/" << comp << ":\n";
                        for (const auto& p : manifest.prompts) {
                            toolInv << "  /" << nsName << "/" << comp << "/" << p.name;
                            if (!p.description.empty())
                                toolInv << " — " << p.description;
                            toolInv << "\n";
                        }
                    }

                    if (!manifest.tools.empty()) {
                        toolInv << "Tools under /" << nsName << "/" << comp << ":\n";
                        std::vector<std::string> toolNames;
                        for (const auto& t : manifest.tools)
                            toolNames.push_back(t.name);
                        std::sort(toolNames.begin(), toolNames.end());
                        size_t perLine = 0;
                        for (const auto& tn : toolNames) {
                            if (perLine == 0) toolInv << "  ";
                            else if (perLine >= 6) { toolInv << "\n  "; perLine = 0; }
                            else toolInv << ", ";
                            toolInv << tn;
                            ++perLine;
                        }
                        toolInv << "\n";
                    }
                }
            }
        }
    }

    // Include actual schemas so the LLM can generate matching ones
    auto actualSchemas = this->schemas();
    std::ostringstream schemaBlock;
    schemaBlock << "Available tool schemas:\n";
    for (const auto& s : actualSchemas) {
        schemaBlock << "  \"" << s.name << "\": " << s.inputSchema.dump(2) << "\n";
    }

    std::string analysisPrompt =
        "Analyze the user's request and recommend skills/tools.\n\n"
        "User request: \"" + promptText + "\"\n\n"
        + toolInv.str() + "\n"
        + schemaBlock.str() + "\n"
        "Output JSON with this structure:\n"
        "```json\n"
        "{\n"
        "  \"intent\": \"<one-line summary>\",\n"
        "  \"plan\": \"<step-by-step execution plan>\",\n"
        "  \"tools\": [\n"
        "    {\n"
        "      \"name\": \"<tool_name>\",\n"
        "      \"schema\": {\n"
        "        \"type\": \"object\",\n"
        "        \"properties\": {\n"
        "          \"<param_name>\": { \"type\": \"<string|boolean|number>\", \"description\": \"<purpose>\" }\n"
        "        },\n"
        "        \"required\": [\"<param_name>\", ...]\n"
        "      }\n"
        "    }\n"
        "  ]\n"
        "}\n"
        "```\n\n"
        "Each tool's schema must exactly match the actual parameter structure of that tool.\n"
        "Tool names must come from the available skills/tools listed above.";

    if (!m_inferenceProvider) {
        return {"Intent analysis unavailable (no inference provider configured).\n\n"
            "User request: \"" + promptText + "\"\n\n"
            + toolInv.str() + "\n"
            "Use show_skills('/') to browse skills.", {}};
    }

    std::string raw = m_inferenceProvider->complete(analysisPrompt, "");

    // Extract JSON block from response (fenced ```json ... ``` or raw)
    std::string jsonStr;
    std::smatch match;
    static const std::regex jsonFence(R"(```(?:json)?\s*\n(.+?)```)");
    if (std::regex_search(raw, match, jsonFence)) {
        jsonStr = match[1].str();
    } else {
        jsonStr = raw;
    }

    // Trim whitespace
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
    };
    trim(jsonStr);

    json parsed;
    try {
        parsed = json::parse(jsonStr);
    } catch (...) {
        return {raw, {}};
    }

    std::string plan = parsed.value("plan", raw);
    SystemToolResult sr;
    sr.output = plan;

    // Validate each recommended tool
    if (parsed.contains("tools") && parsed["tools"].is_array()) {
        bool allValid = true;
        for (const auto& entry : parsed["tools"]) {
            if (!entry.contains("name") || !entry["name"].is_string()) {
                allValid = false;
                break;
            }
            std::string toolName = entry["name"].get<std::string>();

            const ToolSchema* actual = nullptr;
            for (const auto& s : actualSchemas) {
                if (s.name == toolName) { actual = &s; break; }
            }
            if (!actual) {
                allValid = false;
                break;
            }

            if (!entry.contains("schema") || !entry["schema"].is_object()) {
                allValid = false;
                break;
            }
            json genSchema = entry["schema"];
            json actualSchema = actual->inputSchema;

            if (genSchema.value("type", "") != actualSchema.value("type", "")) {
                allValid = false;
                break;
            }

            json genProps = genSchema.value("properties", json::object());
            json actualProps = actualSchema.value("properties", json::object());
            for (auto it = genProps.begin(); it != genProps.end(); ++it) {
                auto actualIt = actualProps.find(it.key());
                if (actualIt == actualProps.end()) {
                    allValid = false;
                    break;
                }
                if (it.value().value("type", "") != actualIt->value("type", "")) {
                    allValid = false;
                    break;
                }
            }
            if (!allValid) break;

            json genRequired = genSchema.value("required", json::array());
            json actualRequired = actualSchema.value("required", json::array());
            std::unordered_set<std::string> genReqSet;
            for (const auto& r : genRequired) genReqSet.insert(r.get<std::string>());
            for (const auto& r : actualRequired) {
                if (genReqSet.find(r.get<std::string>()) == genReqSet.end()) {
                    allValid = false;
                    break;
                }
            }
            if (!allValid) break;

            sr.recommendedTools.push_back(toolName);
        }

        if (!allValid) {
            sr.recommendedTools.clear();
        }
    }

    return sr;
}

} // namespace a0
