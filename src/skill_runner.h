#pragma once

#include "agent_interfaces.h"

class DefaultSkillRunner : public SkillRunner {
public:
    DefaultSkillRunner(ToolRunner* toolRunner,
                       InferenceProvider* provider,
                       ComponentRegistry* registry);

    std::string expandPrompt(const Skill& skill, const json& params) override;
    json runValidators(const Skill& skill, const json& input) override;
    json execute(const Skill& skill, const json& params) override;

private:
    ToolRunner* m_toolRunner;
    InferenceProvider* m_provider;
    ComponentRegistry* m_registry;
};
