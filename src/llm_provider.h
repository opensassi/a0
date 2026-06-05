#pragma once

#include <string>
#include <vector>

#include "agent_interfaces.h"
#include "mpsc.h"

namespace a0 {

/// Abstract async LLM provider interface.
///
/// Non-blocking startRequest() / tick() API designed for event-loop integration.
/// Implementations provide provider-specific API format, auth, and transport.
///
/// Lifecycle: startRequest() -> tick()* until complete -> startRequest() again.
///
class LlmProvider {
public:
    virtual ~LlmProvider() = default;

    /// Start a non-streaming request. Non-blocking -- call tick() to drive progress.
    virtual void startRequest(const std::string& systemPrompt,
                              const std::vector<Message>& messages,
                              const std::vector<ToolSchema>& tools) = 0;

    /// Start a streaming request. Non-blocking.
    virtual void startRequestStreaming(const std::string& systemPrompt,
                                       const std::vector<Message>& messages,
                                       const std::vector<ToolSchema>& tools) = 0;

    /// Drive curl progress. Returns pending events (tokens, tool_calls, complete, error).
    virtual std::vector<mpsc::AppCoreEvent> tick() = 0;

    /// Cancel the in-flight request.
    virtual void cancel() = 0;

    /// True when a request is active.
    virtual bool active() const = 0;

    /// Timeout in ms for the next tick(), or -1 if idle.
    virtual int timeoutMs() const = 0;

    /// Set mock URL for testing.
    virtual void setMockUrl(const std::string& url) = 0;
};

} // namespace a0
