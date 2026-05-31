#include "system_tools/registry.h"
#include "agent_interfaces.h"
#include "skills/skills.h"
#include <sstream>
#include <algorithm>
#include <regex>

namespace a0 {

// ---------------------------------------------------------------------------
// run_skill — load and return skill prompt text
// Path: /system/git/start_session → lookup via SkillManager
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xRunSkill(const json& params) {
    auto pathIt = params.find("path");
    if (pathIt == params.end() || !pathIt->is_string()) {
        return {"ERROR: missing required string parameter 'path'"};
    }
    std::string path = pathIt->get<std::string>();

    std::string rest = path;
    if (rest.front() == '/') rest = rest.substr(1);
    std::vector<std::string> parts;
    std::istringstream ss(rest);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (!seg.empty()) parts.push_back(seg);
    }

    if (parts.size() < 3) {
        return {"ERROR: invalid skill path: " + path + ". Expected /namespace/component/name"};
    }

    std::string ns = parts[0];
    std::string component = parts[1];
    std::string name = parts[2];
    for (size_t i = 3; i < parts.size(); ++i) name += ":" + parts[i];

    if (m_skillManager) {
        std::string qualifiedName = ns + ":" + component + ":" + name;
        Prompt resolved;
        if (m_skillManager->getPromptResolved(qualifiedName, resolved) == 0) {
            return {resolved.prompt};
        }
        for (const auto& tryNs : {"system", "local"}) {
            std::string qn = std::string(tryNs) + ":" + component + ":" + name;
            if (qn != qualifiedName && m_skillManager->getPromptResolved(qn, resolved) == 0) {
                return {resolved.prompt};
            }
        }
        return {"ERROR: skill not found: " + qualifiedName};
    }
    return {"ERROR: no skill manager available. Cannot execute: " + path};
}

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

    out << "\nUse run_skill('/path/to/skill') to execute a skill.";
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
        out << "  /system/ — core tools (bash, read, glob, grep, edit, write, run_skill, etc.)\n";
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
        out << "Use run_skill('/system/git/start_session') for the standard workflow.\n";
    } else if (firstPart == "system") {
        out << "Core system tools: bash, read, glob, grep, edit, write, run_skill, show_skills, show_skill_tools, tools_for_prompt\n";
    }

    return {out.str()};
}

// ---------------------------------------------------------------------------
// tools_for_prompt — analyze user intent and recommend skills/tools
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xToolsForPrompt(const json& params) {
    auto promptIt = params.find("prompt");
    if (promptIt == params.end() || !promptIt->is_string()) {
        return {"ERROR: missing required string parameter 'prompt'"};
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

                    // Prompts (multi-step skills)
                    if (!manifest.prompts.empty()) {
                        toolInv << "Skills under /" << nsName << "/" << comp << ":\n";
                        for (const auto& p : manifest.prompts) {
                            toolInv << "  /" << nsName << "/" << comp << "/" << p.name;
                            if (!p.description.empty())
                                toolInv << " — " << p.description;
                            toolInv << "\n";
                        }
                    }

                    // Tools (individual commands)
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

    std::string analysisPrompt =
        "Analyze the user's request and recommend skills/tools.\n\n"
        "User request: \"" + promptText + "\"\n\n"
        "Determine the user's intent: requesting a specific action, asking a question, "
        "making an observation, venting frustration, reporting a problem, etc.\n\n"
        + toolInv.str() + "\n"
        "If the user is requesting a specific action:\n"
        "1. Review the recommended skills and tools.\n"
        "2. Call run_skill('/path/to/skill') for multi-step workflows.\n"
        "3. For individual operations, call the tool directly by its full path name "
        "(e.g. use system_fs_read for reading files, system_git_status for git status, "
        "system_docker_ps for docker ps).\n"
        "4. Formulate a step-by-step plan.\n\n"
        "Output format:\n"
        "Intent: <one line>\n"
        "Plan: <step-by-step>\n\n"
        "If the request is not an action, state the intent and suggest available options.";

    if (m_inferenceProvider) {
        std::string result = m_inferenceProvider->complete(analysisPrompt, "");
        return {result};
    }

    return {"Intent analysis unavailable (no inference provider configured).\n\n"
        "User request: \"" + promptText + "\"\n\n"
        + toolInv.str() + "\n"
        "Use show_skills('/') to browse skills."};
}

} // namespace a0
