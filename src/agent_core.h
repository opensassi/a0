#pragma once

#include <unordered_set>
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
                     DependencyResolver* depResolver,
                     SchemaInferenceEngine* inferenceEngine,
                     a0::SystemToolRegistry* systemTools,
                     a0::skills::SkillManager* skillMgr,
                     a0::persistence::PersistenceStore* persistence = nullptr,
                     DockerToolRunner* dockerRunner = nullptr,
                     ComposeManager* composeMgr = nullptr);

    bool init(const std::string& skillsDir) override;
    json processGoal(const std::string& goal) override;
    json processGoal(const std::string& goal, const json& params);
    json runSkill(const std::string& skillName, const json& params);
    bool resumeSession(const std::string& sessionId) override;
    std::string currentSessionId() const override;
    void run() override;
    a0::StreamHandle processGoalStreaming(const std::string& goal,
                                           a0::StreamCallback onChunk) override;

private:
    void xPushToContext(const std::string& goal, const json& result);
    void xBuildDispatchTable();

    std::string xRunForkedLoop(
        const std::string& userInput,
        const std::vector<ToolSchema>& tools,
        int maxTurns);

    std::string m_basePrompt;
    int m_agentDbId = -1;
    int64_t m_sessionDbId = 0;
    int64_t m_nextSubSession = 1;

    a0::skills::SkillManager* m_skillMgr;
    ToolRunner* m_toolRunner;
    a0::SystemToolRegistry* m_systemTools;
    a0::persistence::PersistenceStore* m_persistence;
    DockerToolRunner* m_dockerRunner;
    ComposeManager* m_composeMgr;
    SkillRunner* m_skillRunner;
    InferenceProvider* m_provider;
    ContextManager* m_context;
    DependencyResolver* m_depResolver;
    SchemaInferenceEngine* m_inferenceEngine;
    std::string m_sessionId;
    bool m_initialized;

    std::unordered_map<std::string, std::string> m_dispatch;
    std::unordered_set<std::string> m_accumulatedTools;
};
