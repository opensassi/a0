#include "dependency_resolver.h"
#include <set>
#include <algorithm>

DefaultDependencyResolver::DefaultDependencyResolver(const SkillRegistry* registry)
    : m_registry(registry) {}

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
        // Check if dependency is a tool
        if (m_registry->getTool(dep).has_value()) {
            continue;
        }
        // Check if dependency is a prompt
        auto opt = m_registry->getPrompt(dep);
        if (opt.has_value()) {
            auto transitive = missingDependenciesRecursive(*opt, visited);
            missing.insert(missing.end(), transitive.begin(), transitive.end());
        } else {
            missing.push_back(dep);
        }
    }
    return missing;
}
