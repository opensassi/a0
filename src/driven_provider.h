#pragma once

#include <memory>
#include <string>
#include <vector>

#include <curl/curl.h>

#include "agent_interfaces.h"
#include "mpsc.h"
#include "response_decoder.h"

namespace a0 {

/// Async LLM provider using curl_multi.
///
/// Non-blocking startRequest() / tick() API designed for event-loop integration.
/// Builds alongside the existing DeepSeekProvider — does NOT replace it yet.
///
/// Lifecycle: startRequest() → tick()* until complete → startRequest() again.
///
class DrivenProvider {
public:
    DrivenProvider(const std::string& apiKey,
                   const std::string& model = "deepseek-chat");
    ~DrivenProvider();

    DrivenProvider(const DrivenProvider&) = delete;
    DrivenProvider& operator=(const DrivenProvider&) = delete;
    DrivenProvider(DrivenProvider&&) = delete;
    DrivenProvider& operator=(DrivenProvider&&) = delete;

    /// Start a non-streaming request. Non-blocking — call tick() to drive progress.
    void startRequest(const std::string& systemPrompt,
                      const std::vector<Message>& messages,
                      const std::vector<ToolSchema>& tools);

    /// Start a streaming request. Non-blocking.
    void startRequestStreaming(const std::string& systemPrompt,
                               const std::vector<Message>& messages,
                               const std::vector<ToolSchema>& tools);

    /// Drive curl progress. Returns pending events (tokens, tool_calls, complete, error).
    /// Call this from the event loop whenever curl fds are ready or timeout expires.
    std::vector<mpsc::AppCoreEvent> tick();

    /// Cancel the in-flight request.
    void cancel();

    /// True when a request is active.
    bool active() const { return m_active; }

    /// Timeout in ms for the next tick(), or -1 if idle.
    /// Use this as the poll() timeout when waiting on the event_fd.
    int timeoutMs() const;

    /// Set mock URL for testing.
    void setMockUrl(const std::string& url) { m_baseUrl = url; }

    /// Get current mock URL.
    const std::string& mockUrl() const { return m_baseUrl; }

private:
    struct EasyHandle {
        CURL* easy = nullptr;
        curl_slist* headers = nullptr;
        std::string requestBody;   // Must outlive curl easy handle (CURLOPT_POSTFIELDS is a pointer)
        std::string responseBody;
        bool streaming = false;
        ResponseDecoder decoder;
    };

    std::string m_apiKey;
    std::string m_model;
    std::string m_baseUrl;
    CURLM* m_multi = nullptr;
    bool m_active = false;
    EasyHandle m_handle;

    // FDs from curl_multi_fdset for poll() integration
    mutable int m_cachedReadFd = -1;
    mutable int m_cachedWriteFd = -1;
    mutable long m_cachedTimeout = -1;

    void xSetupCommon(CURL* curl, curl_slist*& headers, bool streaming);
    void xBuildPayload(json& payload,
                       const std::string& systemPrompt,
                       const std::vector<Message>& messages,
                       const std::vector<ToolSchema>& tools,
                       bool stream) const;
    void xUpdatePollInfo() const;
    void xProcessCompletion(CURL* easy, CURLcode result, std::vector<mpsc::AppCoreEvent>& out);
};

} // namespace a0
