#pragma once

#include "agent_interfaces.h"

class DefaultDependencyResolver : public DependencyResolver {
public:
    explicit DefaultDependencyResolver(const ComponentRegistry* registry);
    bool checkToolDependencies(const Tool& tool) const override;
    bool checkSkillDependencies(const Skill& skill) const override;
    std::vector<std::string> missingDependencies(const Skill& skill) const override;

private:
    const ComponentRegistry* m_registry;
};
