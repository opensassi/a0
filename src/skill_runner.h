#pragma once

#include "agent_interfaces.h"

class DefaultSkillRunner : public SkillRunner {
public:
    DefaultSkillRunner(ToolRunner* toolRunner,
                       InferenceProvider* provider,
                       SkillRegistry* registry,
                       DependencyResolver* depResolver,
                       DockerToolRunner* dockerRunner = nullptr,
                       ComposeManager* composeMgr = nullptr);

    std::string expandPrompt(const Prompt& prompt, const json& params) override;
    json runValidators(const Prompt& prompt, const json& input) override;
    json execute(const Prompt& prompt, const json& params) override;

    void setSkillsDir(const std::string& path);

private:
    ToolRunner* m_toolRunner;
    DockerToolRunner* m_dockerRunner;
    ComposeManager* m_composeMgr;
    InferenceProvider* m_provider;
    SkillRegistry* m_registry;
    DependencyResolver* m_depResolver;
    std::string m_skillsDir;
};
