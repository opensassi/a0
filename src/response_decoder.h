#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nlohmann/json.hpp>

#include "mpsc.h"

namespace a0 {

/// Stateful SSE / JSON response decoder.
///
/// Feed raw bytes via feed(), then retrieve structured events via events().
/// Mode is determined by the first bytes: "data: " prefix → SSE, else JSON.
///
/// SSE events:
///   data: {"choices":[{"delta":{"content":"..."}}]}   → LlmToken
///   data: {"choices":[{"delta":{"tool_calls":[...]}}]} → ToolStart
///   data: {"finish_reason":"stop"}                     → Complete
///   data: [DONE]                                       → Complete (silent)
///
/// JSON (non-streaming):
///   {"choices":[{"message":{"content":"..."}}]}        → Complete
///   {"choices":[{"message":{"tool_calls":[...]}}]}     → ToolStart
///
class ResponseDecoder {
public:
    enum class Mode {
        Unknown,
        SSE,
        JSON
    };

    /// Feed raw bytes. May produce zero or more events.
    void feed(const char* data, size_t len);

    /// Feed a string.
    void feed(const std::string& data) { feed(data.data(), data.size()); }

    /// Return and consume all accumulated events.
    std::vector<mpsc::AppCoreEvent> events();

    /// True when a complete response or error has been decoded.
    bool complete() const { return m_complete; }

    /// Reset decoder state for a new request.
    void reset();

    /// Decode a complete JSON response into events (for non-streaming path).
    static std::vector<mpsc::AppCoreEvent> decodeJson(const std::string& body);

private:
    std::string m_buffer;
    Mode m_mode = Mode::Unknown;
    bool m_complete = false;

    // Tool call accumulation (tool_calls arrive as complete objects in a single delta)
    std::string m_accumToolName;
    std::string m_accumToolArgs;
    std::string m_accumToolId;

    void xFlushLine(const std::string& line);
    void xFlushBuffer();
    void xProcessJsonChunk(const nlohmann::json& j);

    std::vector<mpsc::AppCoreEvent> m_events;
};

} // namespace a0
