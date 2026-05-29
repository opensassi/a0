#pragma once

#include "agent_interfaces.h"
#include "system_tools.h"

class DefaultSkillRunner : public SkillRunner {
public:
    DefaultSkillRunner(ToolRunner* toolRunner,
                       InferenceProvider* provider,
                       SkillRegistry* registry,
                       DependencyResolver* depResolver,
                       a0::SystemToolRegistry* systemTools = nullptr,
                       DockerToolRunner* dockerRunner = nullptr,
                       ComposeManager* composeMgr = nullptr);

    std::string expandPrompt(const Prompt& prompt, const json& params) override;
    json runValidators(const Prompt& prompt, const json& input) override;
    json execute(const Prompt& prompt, const json& params) override;

    void setSkillsDir(const std::string& path);

private:
    void xRebuildBasePrompt();
    std::string m_basePrompt;

    ToolRunner* m_toolRunner;
    a0::SystemToolRegistry* m_systemTools;
    DockerToolRunner* m_dockerRunner;
    ComposeManager* m_composeMgr;
    InferenceProvider* m_provider;
    SkillRegistry* m_registry;
    DependencyResolver* m_depResolver;
    std::string m_skillsDir;
};
