#pragma once

#include "agent_interfaces.h"
#include "system_tools.h"
#include "skills/skills.h"

namespace a0::persistence { class PersistenceStore; }

class DefaultAgentCore : public AgentCore {
public:
    DefaultAgentCore(ToolRunner* toolRunner,
                     SkillRunner* skillRunner,
                     InferenceProvider* provider,
                     ContextManager* context,
                     InvocationLogger* logger,
                     DependencyResolver* depResolver,
                     SchemaInferenceEngine* inferenceEngine,
                     a0::SystemToolRegistry* systemTools,
                     a0::skills::SkillManager* skillMgr,
                     a0::persistence::PersistenceStore* persistence = nullptr,
                     DockerToolRunner* dockerRunner = nullptr,
                     ComposeManager* composeMgr = nullptr);

    bool init(const std::string& skillsDir) override;
    json processGoal(const std::string& goal) override;
    json runSkill(const std::string& skillName, const json& params);
    bool resumeSession(const std::string& sessionId) override;
    std::string currentSessionId() const override;
    void run() override;

private:
    void xLogAndPush(const std::string& goal, const json& result);
    void xBuildDispatchTable();

    std::string m_basePrompt;
    int m_agentDbId = -1;
    int64_t m_sessionDbId = 0;

    a0::skills::SkillManager* m_skillMgr;
    ToolRunner* m_toolRunner;
    a0::SystemToolRegistry* m_systemTools;
    a0::persistence::PersistenceStore* m_persistence;
    DockerToolRunner* m_dockerRunner;
    ComposeManager* m_composeMgr;
    SkillRunner* m_skillRunner;
    InferenceProvider* m_provider;
    ContextManager* m_context;
    InvocationLogger* m_logger;
    DependencyResolver* m_depResolver;
    SchemaInferenceEngine* m_inferenceEngine;
    std::string m_sessionId;
    bool m_initialized;

    /// Dispatch: short LLM-facing name → qualified internal name
    std::unordered_map<std::string, std::string> m_dispatch;
};
