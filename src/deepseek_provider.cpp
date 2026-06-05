#include "deepseek_provider.h"
#include "trace.h"
#include <cstdlib>

namespace a0 {

DeepSeekProvider::DeepSeekProvider(const std::string& apiKey,
                                   const std::string& model)
    : DrivenProvider(apiKey.empty() ? (std::getenv("DEEPSEEK_API_KEY") ? std::getenv("DEEPSEEK_API_KEY") : "") : apiKey,
                     model)
{
    m_baseUrl = "https://api.deepseek.com/v1/chat/completions";
}

void DeepSeekProvider::xBuildPayload(json& payload,
                                      const std::string& systemPrompt,
                                      const std::vector<Message>& messages,
                                      const std::vector<ToolSchema>& tools,
                                      bool stream) const {
    payload["model"] = m_model;
    payload["stream"] = stream;
    payload["max_tokens"] = 4096;

    json msgs = json::array();
    if (!systemPrompt.empty()) {
        msgs.push_back({{"role", "system"}, {"content", systemPrompt}});
    }
    for (const auto& msg : messages) {
        json jmsg;
        jmsg["role"] = msg.role;
        jmsg["content"] = msg.content;
        if (!msg.toolCallId.empty()) {
            jmsg["tool_call_id"] = msg.toolCallId;
        }
        if (!msg.toolCalls.empty()) {
            json tcs = json::array();
            for (const auto& tc : msg.toolCalls) {
                tcs.push_back({
                    {"id", tc.id},
                    {"type", "function"},
                    {"function", {
                        {"name", tc.name},
                        {"arguments", tc.arguments.dump()}
                    }}
                });
            }
            jmsg["tool_calls"] = tcs;
        }
        msgs.push_back(jmsg);
    }
    payload["messages"] = msgs;

    if (!tools.empty()) {
        json toolsArray = json::array();
        for (const auto& t : tools) {
            toolsArray.push_back({
                {"type", "function"},
                {"function", {
                    {"name", t.name},
                    {"description", t.description},
                    {"parameters", t.inputSchema}
                }}
            });
        }
        payload["tools"] = toolsArray;
    }
}

void DeepSeekProvider::xAddAuth(curl_slist*& headers) {
    std::string authHeader = "Authorization: Bearer " + m_apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());
}

} // namespace a0
