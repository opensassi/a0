#pragma once

#include "llm/driven_provider.h"

namespace a0 {

/// DeepSeek-specific LLM provider.
///
/// Implements the OpenAI-compatible API format used by DeepSeek.
/// API key resolved from constructor argument or DEEPSEEK_API_KEY env var.
/// Base URL: https://api.deepseek.com/v1/chat/completions
///
class DeepSeekProvider : public DrivenProvider {
public:
    explicit DeepSeekProvider(const std::string& apiKey = "",
                              const std::string& model = "deepseek-chat");

protected:
    void xBuildPayload(json& payload,
                       const std::string& systemPrompt,
                       const std::vector<Message>& messages,
                       const std::vector<ToolSchema>& tools,
                       bool stream) const override;

    void xAddAuth(curl_slist*& headers) override;
};

} // namespace a0
