#pragma once

#include "agent_interfaces.h"
#include <set>

class DefaultDependencyResolver : public DependencyResolver {
public:
    explicit DefaultDependencyResolver(const SkillRegistry* registry);
    bool checkToolDependencies(const Tool& tool) const override;
    bool checkPromptDependencies(const Prompt& prompt) const override;
    std::vector<std::string> missingDependencies(const Prompt& prompt) const override;

private:
    const SkillRegistry* m_registry;
    std::vector<std::string> missingDependenciesRecursive(
        const Prompt& prompt, std::set<std::string>& visited) const;
};
