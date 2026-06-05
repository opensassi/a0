#pragma once

#include <memory>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "agent_interfaces.h"
#include "llm_provider.h"
#include "mpsc.h"
#include "response_decoder.h"

namespace a0 {

/// Universal async LLM provider using curl_multi.
///
/// Non-blocking startRequest() / tick() API designed for event-loop integration.
/// Concrete subclasses provide provider-specific API format and auth via
/// the pure virtual hooks xBuildPayload() and xAddAuth().
///
/// Lifecycle: startRequest() -> tick()* until complete -> startRequest() again.
///
class DrivenProvider : public LlmProvider {
public:
    DrivenProvider(const std::string& apiKey,
                   const std::string& model = "deepseek-chat");
    ~DrivenProvider() override;

    DrivenProvider(const DrivenProvider&) = delete;
    DrivenProvider& operator=(const DrivenProvider&) = delete;
    DrivenProvider(DrivenProvider&&) = delete;
    DrivenProvider& operator=(DrivenProvider&&) = delete;

    // -- LlmProvider implementation --

    void startRequest(const std::string& systemPrompt,
                      const std::vector<Message>& messages,
                      const std::vector<ToolSchema>& tools) override;

    void startRequestStreaming(const std::string& systemPrompt,
                               const std::vector<Message>& messages,
                               const std::vector<ToolSchema>& tools) override;

    std::vector<mpsc::AppCoreEvent> tick() override;

    void cancel() override;

    bool active() const override { return m_active; }

    int timeoutMs() const override;

    void setMockUrl(const std::string& url) override { m_baseUrl = url; }

    /// Get current mock URL.
    const std::string& mockUrl() const { return m_baseUrl; }

protected:
    // -- Hooks for subclasses --

    /// Build the provider-specific JSON request payload.
    virtual void xBuildPayload(json& payload,
                               const std::string& systemPrompt,
                               const std::vector<Message>& messages,
                               const std::vector<ToolSchema>& tools,
                               bool stream) const = 0;

    /// Add provider-specific auth headers (e.g. "Authorization: Bearer ...").
    virtual void xAddAuth(curl_slist*& headers) = 0;

    // -- Shared state for subclasses --

    std::string m_apiKey;
    std::string m_model;
    std::string m_baseUrl;

private:
    struct EasyHandle {
        CURL* easy = nullptr;
        curl_slist* headers = nullptr;
        std::string requestBody;
        std::string responseBody;
        bool streaming = false;
        ResponseDecoder decoder;
    };

    CURLM* m_multi = nullptr;
    bool m_active = false;
    EasyHandle m_handle;

    mutable int m_cachedReadFd = -1;
    mutable int m_cachedWriteFd = -1;
    mutable long m_cachedTimeout = -1;

    void xSetupCommon(CURL* curl, curl_slist*& headers, bool streaming);
    void xUpdatePollInfo() const;
    void xProcessCompletion(CURL* easy, CURLcode result, std::vector<mpsc::AppCoreEvent>& out);
};

} // namespace a0
