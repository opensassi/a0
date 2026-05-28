#pragma once

#include "agent_interfaces.h"
#include <set>

class DefaultDependencyResolver : public DependencyResolver {
public:
    explicit DefaultDependencyResolver(const ComponentRegistry* registry);
    bool checkToolDependencies(const Tool& tool) const override;
    bool checkSkillDependencies(const Skill& skill) const override;
    std::vector<std::string> missingDependencies(const Skill& skill) const override;

private:
    std::vector<std::string> missingDependenciesRecursive(
        const Skill& skill, std::set<std::string>& visited) const;
    const ComponentRegistry* m_registry;
};
