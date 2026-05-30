#pragma once

#include "agent_interfaces.h"
#include "skills/skills.h"
#include <set>

class DefaultDependencyResolver : public DependencyResolver {
public:
    explicit DefaultDependencyResolver(const a0::skills::SkillManager* skillMgr);
    bool checkToolDependencies(const Tool& tool) const override;
    bool checkPromptDependencies(const Prompt& prompt) const override;
    std::vector<std::string> missingDependencies(const Prompt& prompt) const override;

private:
    const a0::skills::SkillManager* m_skillMgr;
    std::vector<std::string> missingDependenciesRecursive(
        const Prompt& prompt, std::set<std::string>& visited) const;
};
