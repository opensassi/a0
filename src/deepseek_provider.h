#pragma once

#include "agent_interfaces.h"

class DeepSeekProvider : public InferenceProvider {
public:
    DeepSeekProvider(const std::string& apiKey, const std::string& model = "deepseek-chat");
    std::string complete(const std::string& systemPrompt,
                          const std::string& userPrompt) override;
    void setMockUrl(const std::string& url) override;

private:
    std::string m_apiKey;
    std::string m_model;
    std::string m_baseUrl;
};
