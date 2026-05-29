#pragma once

#include "agent_interfaces.h"

class DefaultSchemaInferenceEngine : public SchemaInferenceEngine {
public:
    explicit DefaultSchemaInferenceEngine(InferenceProvider* provider);
    Tool inferTool(const std::string& naturalLanguageDescription) override;
    Prompt inferPrompt(const std::string& naturalLanguageDescription) override;

private:
    InferenceProvider* m_provider;
};
