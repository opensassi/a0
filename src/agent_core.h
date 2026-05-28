#pragma once

#include "agent_interfaces.h"

class DefaultAgentCore : public AgentCore {
public:
    DefaultAgentCore(ComponentRegistry* registry,
                     ToolRunner* toolRunner,
                     SkillRunner* skillRunner,
                     InferenceProvider* provider,
                     ContextManager* context,
                     InvocationLogger* logger,
                     DependencyResolver* depResolver,
                     SchemaInferenceEngine* inferenceEngine);

    bool init(const std::string& componentsDir) override;
    json processGoal(const std::string& goal) override;
    bool resumeSession(const std::string& sessionId) override;
    std::string currentSessionId() const override;
    void run() override;

private:
    ComponentRegistry* m_registry;
    ToolRunner* m_toolRunner;
    SkillRunner* m_skillRunner;
    InferenceProvider* m_provider;
    ContextManager* m_context;
    InvocationLogger* m_logger;
    DependencyResolver* m_depResolver;
    SchemaInferenceEngine* m_inferenceEngine;
    std::string m_sessionId;
    bool m_initialized;
};
