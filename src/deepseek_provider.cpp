#include "deepseek_provider.h"
#include "trace.h"
#include <curl/curl.h>
#include <sstream>
#include <iostream>

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
    return total;
}

DeepSeekProvider::DeepSeekProvider(const std::string& apiKey, const std::string& model)
    : m_apiKey(apiKey)
    , m_model(model)
    , m_baseUrl("https://api.deepseek.com/v1/chat/completions") {}

std::string DeepSeekProvider::complete(const std::string& systemPrompt,
                                        const std::string& userPrompt) {
    TRACE_LOG("complete(system=" << systemPrompt.size() << " user=" << userPrompt.size() << ")");
    TRACE_LOG("userPrompt=" << userPrompt);
    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string authHeader = "Authorization: Bearer " + m_apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());

    json payload;
    payload["model"] = m_model;
    json messages = json::array();
    if (!systemPrompt.empty()) {
        messages.push_back({{"role", "system"}, {"content", systemPrompt}});
    }
    messages.push_back({{"role", "user"}, {"content", userPrompt}});
    payload["messages"] = messages;
    payload["max_tokens"] = 4096;

    std::string body = payload.dump();
    TRACE_LOG("request body: " << body);

    curl_easy_setopt(curl, CURLOPT_URL, m_baseUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // skip SSL verification for mock URLs
    if (m_baseUrl.find("localhost") != std::string::npos ||
        m_baseUrl.find("127.0.0.1") != std::string::npos) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return {};
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    TRACE_LOG("response: " << response);

    try {
        json jsonResp = json::parse(response);
        if (jsonResp.contains("error")) {
            return {};
        }
        return jsonResp["choices"][0]["message"]["content"].get<std::string>();
    } catch (...) {
        return {};
    }

    return {};
}

CompletionResponse DeepSeekProvider::complete(
    const std::string& systemPrompt,
    const std::vector<Message>& messages,
    const std::vector<ToolSchema>& tools)
{
    TRACE_LOG("complete(tools=" << tools.size() << " messages=" << messages.size() << ")");

    CURL* curl = curl_easy_init();
    if (!curl) return {};

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string authHeader = "Authorization: Bearer " + m_apiKey;
    headers = curl_slist_append(headers, authHeader.c_str());

    json payload;
    payload["model"] = m_model;

    // Build messages array: system first, then history
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
    payload["max_tokens"] = 4096;

    // Build tools array
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

    std::string body = payload.dump();
    TRACE_LOG("tool-call request body: " << body);

    curl_easy_setopt(curl, CURLOPT_URL, m_baseUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    if (m_baseUrl.find("localhost") != std::string::npos ||
        m_baseUrl.find("127.0.0.1") != std::string::npos) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {"API request failed: " + std::string(curl_easy_strerror(res)), {}};
    }

    try {
        json jsonResp = json::parse(response);
        if (jsonResp.contains("error")) {
            return {"API error: " + jsonResp["error"]["message"].get<std::string>(), {}};
        }

        json choice = jsonResp["choices"][0];
        json delta = choice["message"];

        CompletionResponse result;

        // Check for tool_calls
        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (const auto& tc : delta["tool_calls"]) {
                ToolCall call;
                call.id = tc["id"].get<std::string>();
                call.name = tc["function"]["name"].get<std::string>();
                try {
                    call.arguments = json::parse(tc["function"]["arguments"].get<std::string>());
                } catch (...) {
                    call.arguments = tc["function"]["arguments"];
                }
                result.toolCalls.push_back(call);
            }
        }

        // Check for text content (may be null when tool_calls are present)
        if (delta.contains("content") && !delta["content"].is_null()) {
            result.content = delta["content"].get<std::string>();
        } else if (!result.content.empty()) {
            // Keep any existing content (prev content from streaming accumulation)
        }

        return result;

    } catch (const std::exception& e) {
        return {"Parse error: " + std::string(e.what()), {}};
    }
}

void DeepSeekProvider::setMockUrl(const std::string& url) {
    TRACE_LOG("setMockUrl(" << url << ")");
    m_baseUrl = url;
}
