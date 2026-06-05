# ResponseDecoder Spec

## 1. Overview

Stateful SSE / JSON response decoder for LLM API responses. `feed()` accepts raw bytes and detects streaming (SSE `data: ...`) vs. non-streaming (JSON) mode from the first bytes. Accumulated events are retrieved via `events()` and include tokens, tool calls, completion, and errors.

**Source files:** `src/response_decoder.h/.cpp`

**Dependencies:** `nlohmann/json`, `mpsc.h`

## 2. Component Specifications

```cpp
namespace a0 {

class ResponseDecoder {
public:
    enum class Mode { Unknown, SSE, JSON };

    /// Feed raw bytes. May produce zero or more events.
    void feed(const char* data, size_t len);
    void feed(const std::string& data);

    /// Return and consume all accumulated events.
    std::vector<mpsc::AppCoreEvent> events();

    /// True when a complete response or error has been decoded.
    bool complete() const { return m_complete; }

    /// Reset decoder state for a new request.
    void reset();

    /// Decode a complete JSON response into events (non-streaming path).
    static std::vector<mpsc::AppCoreEvent> decodeJson(const std::string& body);

private:
    std::string m_buffer;
    Mode m_mode = Mode::Unknown;
    bool m_complete = false;
    std::string m_accumToolName;
    std::string m_accumToolArgs;
    std::string m_accumToolId;

    void xFlushLine(const std::string& line);
    void xFlushBuffer();
    void xProcessJsonChunk(const nlohmann::json& j);

    std::vector<mpsc::AppCoreEvent> m_events;
};

} // namespace a0
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Input
        RAW[Raw bytes]
        FEED[feed()]
    end

    subgraph Mode_Detection
        UNKNOWN[Mode::Unknown]
        DECIDE{prefix data: ?}
        SSE[Mode::SSE]
        JSON[Mode::JSON]
    end

    subgraph SSE_Path
        BUFFER[Line buffer]
        FLUSH[xFlushLine]
        PARSE[json::parse]
        PROCESS[xProcessJsonChunk]
    end

    subgraph JSON_Path
        ACCUM[Accumulate body]
        FLUSHBUF[xFlushBuffer]
    end

    subgraph Output
        EVENTS[events()]
    end

    RAW --> FEED
    FEED --> DECIDE
    DECIDE -->|yes| SSE
    DECIDE -->|no| JSON
    SSE --> BUFFER --> FLUSH --> PARSE --> PROCESS
    JSON --> ACCUM --> FLUSHBUF --> PROCESS
    PROCESS --> EVENTS
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant C as Caller
    participant RD as ResponseDecoder
    participant JSON as nlohmann::json

    Note over C,RD: SSE mode
    C->>RD: feed("data: {\"choices\"...}\n\n")
    RD->>RD: xFlushLine("data: {...}")
    RD->>JSON: json::parse(payload)
    JSON-->>RD: parsed json
    RD->>RD: xProcessJsonChunk(j)
    RD-->>RD: push LlmToken / ToolStart / etc.

    C->>RD: events()
    RD-->>C: vector&lt;AppCoreEvent&gt;

    Note over C,RD: JSON mode
    C->>RD: feed("{\"choices\"...}")
    RD->>RD: buffer.append(data)
    C->>RD: events()
    RD->>RD: xFlushBuffer() → parse → process
    RD-->>C: vector&lt;AppCoreEvent&gt;
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| SSE token chunk | feed() produces `LlmToken` with correct text |
| SSE tool call delta | feed() produces `ToolStart` with name and args |
| SSE finish_reason:stop | complete() returns true, `Complete` emitted |
| SSE `[DONE]` marker | complete() returns true, no malformed parse |
| JSON non-streaming | decodeJson() returns correct events |
| Error in JSON response | `Error` event emitted, complete() = true |
| Malformed JSON chunk | Swallowed silently (no crash) |
| reset() after complete | State cleared, ready for new request |
| Mode auto-detection | First 6 bytes `data: ` → SSE, else JSON |
| Multiple feed() calls | Tokens accumulate incrementally |
