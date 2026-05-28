#include "dependency_resolver.h"
#include "trace.h"

DefaultDependencyResolver::DefaultDependencyResolver(const ComponentRegistry* registry)
    : m_registry(registry) {}

bool DefaultDependencyResolver::checkToolDependencies(const Tool& tool) const {
    TRACE_LOG("checkToolDependencies(" << tool.name << ")");
    (void)tool;
    return true;
}

bool DefaultDependencyResolver::checkSkillDependencies(const Skill& skill) const {
    TRACE_LOG("checkSkillDependencies(" << skill.name << ")");
    return missingDependencies(skill).empty();
}

std::vector<std::string> DefaultDependencyResolver::missingDependencies(const Skill& skill) const {
    TRACE_LOG("missingDependencies(" << skill.name << ")");
    std::set<std::string> visited;
    return missingDependenciesRecursive(skill, visited);
}

std::vector<std::string> DefaultDependencyResolver::missingDependenciesRecursive(
    const Skill& skill, std::set<std::string>& visited) const
{
    TRACE_LOG("missingDependenciesRecursive(" << skill.name << ")");
    std::vector<std::string> missing;
    for (const auto& dep : skill.dependencies) {
        if (visited.find(dep) != visited.end()) continue;
        visited.insert(dep);

        if (m_registry->getTool(dep).has_value()) continue;

        auto skillOpt = m_registry->getSkill(dep);
        if (skillOpt.has_value()) {
            auto subMissing = missingDependenciesRecursive(*skillOpt, visited);
            missing.insert(missing.end(), subMissing.begin(), subMissing.end());
        } else {
            missing.push_back(dep);
        }
    }
    return missing;
}
