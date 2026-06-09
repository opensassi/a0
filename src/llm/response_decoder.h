#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "shared/mpsc.h"
#include "shared/resource_provider.h"

namespace a0 {

/// Stateful SSE / JSON response decoder.
///
/// Feed raw bytes via feed(), then retrieve structured events via events().
/// Mode is determined by the first bytes: "data: " prefix → SSE, else JSON.
///
/// SSE events:
///   data: {"choices":[{"delta":{"content":"..."}}]}   → LlmChunk (buffered)
///   data: {"choices":[{"delta":{"tool_calls":[...]}}]} → ToolStart (new style)
///   data: {"finish_reason":"stop"}                     → LlmComplete
///   data: [DONE]                                       → LlmComplete (silent)
///
/// JSON (non-streaming):
///   {"choices":[{"message":{"content":"..."}}]}        → LlmComplete
///   {"choices":[{"message":{"tool_calls":[...]}}]}     → ToolStart (new style)
///
/// Tokens are buffered and flushed to a ResourceWriter at tokenFlushSize intervals.
/// LlmStart is emitted on first data, LlmChunk on each flush, LlmComplete on finish.
class ResponseDecoder {
public:
    enum class Mode {
        Unknown,
        SSE,
        JSON
    };

    /// \param provider        ResourceProvider for creating LlmStream resources (nullable).
    /// \param tokenFlushSize  Bytes accumulated before flushing to ResourceWriter (default 256).
    explicit ResponseDecoder(ResourceProvider* provider = nullptr,
                             int64_t tokenFlushSize = 256);
    ~ResponseDecoder();

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

    /// Get the current stream ID (0 if no stream active).
    int64_t streamId() const { return m_streamId; }

    /// Get the current round sequence number.
    int roundSeq() const { return m_roundSeq; }

    /// Decode a complete JSON response into events (for non-streaming path).
    static std::vector<mpsc::AppCoreEvent> decodeJson(const std::string& body);

private:
    std::string m_buffer;
    Mode m_mode = Mode::Unknown;
    bool m_complete = false;

    ResourceProvider* m_provider = nullptr;
    std::unique_ptr<ResourceWriter> m_writer;
    int64_t m_tokenFlushSize = 256;
    int64_t m_streamId = 0;
    int m_roundSeq = 0;
    int m_chunkSeq = 0;
    std::string m_textBuffer;
    bool m_startEmitted = false;

    struct AccumToolCall {
        std::string id;
        std::string name;
        std::string args;
    };
    std::unordered_map<int, AccumToolCall> m_accumToolCalls;

    void xFlushLine(const std::string& line);
    void xFlushBuffer();
    void xProcessJsonChunk(const nlohmann::json& j);
    void xEmitStart();
    void xEmitChunk(bool isFinal);
    void xFlushText(bool isFinal);

    std::vector<mpsc::AppCoreEvent> m_events;
};

} // namespace a0
