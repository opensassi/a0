#include "dependency_resolver.h"
#include "skills/skills.h"
#include <set>
#include <algorithm>

DefaultDependencyResolver::DefaultDependencyResolver(const a0::skills::SkillManager* skillMgr)
    : m_skillMgr(skillMgr) {}

bool DefaultDependencyResolver::checkToolDependencies(const Tool& tool) const {
    (void)tool;
    return true;
}

bool DefaultDependencyResolver::checkPromptDependencies(const Prompt& prompt) const {
    return missingDependencies(prompt).empty();
}

std::vector<std::string> DefaultDependencyResolver::missingDependencies(
    const Prompt& prompt) const {
    std::set<std::string> visited;
    return missingDependenciesRecursive(prompt, visited);
}

std::vector<std::string> DefaultDependencyResolver::missingDependenciesRecursive(
    const Prompt& prompt, std::set<std::string>& visited) const {
    if (visited.find(prompt.name) != visited.end()) {
        return {};
    }
    visited.insert(prompt.name);

    std::vector<std::string> missing;
    for (const auto& dep : prompt.dependencies) {
        // Check if dependency is a tool (qualified lookup)
        a0::skills::SkillTool tool;
        if (m_skillMgr && m_skillMgr->getTool(dep, tool) == 0) {
            continue;
        }
        // Check if dependency is a prompt
        Prompt sp;
        if (m_skillMgr && m_skillMgr->getPrompt(dep, sp) == 0) {
            auto transitive = missingDependenciesRecursive(sp, visited);
            missing.insert(missing.end(), transitive.begin(), transitive.end());
        } else {
            missing.push_back(dep);
        }
    }
    return missing;
}
