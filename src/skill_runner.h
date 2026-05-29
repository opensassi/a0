#pragma once

#include "agent_interfaces.h"

class DefaultSkillRunner : public SkillRunner {
public:
    DefaultSkillRunner(ToolRunner* toolRunner,
                       InferenceProvider* provider,
                       ComponentRegistry* registry,
                       DependencyResolver* depResolver = nullptr,
                       DockerToolRunner* dockerRunner = nullptr,
                       ComposeManager* composeMgr = nullptr);

    std::string expandPrompt(const Skill& skill, const json& params) override;
    json runValidators(const Skill& skill, const json& input) override;
    json execute(const Skill& skill, const json& params) override;

    void setComponentsDir(const std::string& path);

private:
    ToolRunner* m_toolRunner;
    DockerToolRunner* m_dockerRunner;
    ComposeManager* m_composeMgr;
    InferenceProvider* m_provider;
    ComponentRegistry* m_registry;
    DependencyResolver* m_depResolver;
    std::string m_componentsDir;
};
