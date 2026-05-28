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
    std::vector<std::string> missing;
    for (const auto& dep : skill.dependencies) {
        if (!m_registry->getTool(dep).has_value() &&
            !m_registry->getSkill(dep).has_value()) {
            missing.push_back(dep);
        }
    }
    return missing;
}
