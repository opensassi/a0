#pragma once

#include "agent_interfaces.h"
#include "skills/skills.h"
#include <unordered_map>

class DefaultSkillRunner : public SkillRunner {
public:
    DefaultSkillRunner(ToolRunner* toolRunner,
                       InferenceProvider* provider,
                       a0::skills::SkillManager* skillMgr,
                       DependencyResolver* depResolver,
                       DockerToolRunner* dockerRunner = nullptr,
                       ComposeManager* composeMgr = nullptr);

    std::string expandPrompt(const Prompt& prompt, const json& params) override;
    json runValidators(const Prompt& prompt, const json& input) override;
    json execute(const Prompt& prompt, const json& params) override;

    a0::StreamHandle executeStreaming(const Prompt& prompt,
                                       const json& params,
                                       a0::StreamCallback onChunk) override;

    void setSkillsDir(const std::string& path);
    void setGlobalVar(const std::string& key, const std::string& value);
    void setGlobalVars(const std::unordered_map<std::string, std::string>& vars);
    void setMaxParallel(int n) { m_maxParallel = n; }

private:
    void xRebuildBasePrompt();
    std::string m_basePrompt;

    ToolRunner* m_toolRunner;
    DockerToolRunner* m_dockerRunner;
    ComposeManager* m_composeMgr;
    InferenceProvider* m_provider;
    a0::skills::SkillManager* m_skillMgr;
    DependencyResolver* m_depResolver;
    std::string m_skillsDir;

    std::unordered_map<std::string, std::string> m_globalVars;
    int m_maxParallel = 4;
};
