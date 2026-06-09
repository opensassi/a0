# ResponseDecoder Spec

## 1. Overview

Stateful SSE / JSON response decoder for LLM API responses. `feed()` accepts raw bytes and detects streaming (SSE) vs. non-streaming (JSON) mode from the first bytes. Integrates with `ResourceProvider` for streaming token persistence — tokens are buffered and flushed to a `ResourceWriter` at configurable intervals.

**Source files:** `src/llm/response_decoder.h/.cpp`

**Dependencies:** `nlohmann/json`, `shared/mpsc.h`, `shared/resource_provider.h`

## 2. Component Specifications

```cpp
namespace a0 {

class ResponseDecoder {
public:
    enum class Mode { Unknown, SSE, JSON };

    explicit ResponseDecoder(ResourceProvider* provider = nullptr,
                             int64_t tokenFlushSize = 256);
    ~ResponseDecoder();

    void feed(const char* data, size_t len);
    void feed(const std::string& data);
    std::vector<mpsc::AppCoreEvent> events();
    bool complete() const { return m_complete; }
    void reset();
    int64_t streamId() const { return m_streamId; }
    int roundSeq() const { return m_roundSeq; }

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
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Input
        RAW[Raw bytes]
        FEED[feed()]
    end

    subgraph Mode_Detection
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

    subgraph Resource_Integration
        RP[ResourceProvider]
        WRITER[ResourceWriter]
        EMIT[xEmitStart / xEmitChunk]
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
    PROCESS --> EMIT
    RP -->|create| WRITER
    EMIT -->|flush to| WRITER
    PROCESS --> EVENTS
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant C as Caller
    participant RD as ResponseDecoder
    participant RP as ResourceProvider
    participant JSON as nlohmann::json

    Note over C,RD: SSE mode
    C->>RD: feed("data: {\"choices\"...}\n\n")
    RD->>RD: xFlushLine("data: {...}")
    RD->>JSON: json::parse(payload)
    JSON-->>RD: parsed json
    RD->>RD: xProcessJsonChunk(j)
    RD->>RD: xEmitStart() → LlmStart
    RD->>RD: xEmitChunk() → LlmChunk + write to ResourceWriter
    RD->>RD: push ToolStart / ToolChunk / ToolEnd

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
| SSE token chunk | feed() produces LlmChunk with correct text |
| SSE tool call delta | feed() produces ToolStart with name and args |
| SSE finish_reason:stop | complete() returns true, LlmComplete emitted |
| SSE [DONE] marker | complete() returns true, no malformed parse |
| JSON non-streaming | decodeJson() returns correct events |
| Error in JSON response | Error event emitted, complete() = true |
| Malformed JSON chunk | Swallowed silently (no crash) |
| reset() after complete | State cleared, ready for new request |
| Mode auto-detection | First bytes data: → SSE, else JSON |
| Multiple feed() calls | Tokens accumulate incrementally |
| ResourceProvider integration | Tokens flushed to ResourceWriter at tokenFlushSize |
| streamId() after first data | Returns non-zero |
| roundSeq() | Returns correct round sequence number |
