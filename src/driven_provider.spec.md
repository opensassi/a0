# DrivenProvider Spec

## 1. Overview

Async LLM provider using `curl_multi` for non-blocking HTTP requests. Supports both streaming and non-streaming modes. Designed for event-loop integration: `startRequest()` / `startRequestStreaming()` kick off transfers, `tick()` drives curl progress and returns decoded events, `timeoutMs()` provides the poll timeout.

**Source files:** `src/driven_provider.h/.cpp`

**Dependencies:** `libcurl`, `nlohmann/json`, `response_decoder.h`, `mpsc.h`, `agent_interfaces.h`

**Important:** `tick()` must call `curl_multi_wait(m_multi, nullptr, 0, 0, nullptr)` before
`curl_multi_perform()` to drive curl's internal async DNS resolver and connection I/O.
Without this, the async resolver's sockets are never polled, DNS queries hang until
timeout, and curl reports `CURLE_COULDNT_RESOLVE_HOST`. The zero-timeout wait is a
single non-blocking `poll()` call (~1µs).

## 2. Component Specifications

```cpp
namespace a0 {

class DrivenProvider {
public:
    DrivenProvider(const std::string& apiKey,
                   const std::string& model = "deepseek-chat");
    ~DrivenProvider();

    DrivenProvider(const DrivenProvider&) = delete;
    DrivenProvider& operator=(const DrivenProvider&) = delete;
    DrivenProvider(DrivenProvider&&) = delete;
    DrivenProvider& operator=(DrivenProvider&&) = delete;

    void startRequest(const std::string& systemPrompt,
                      const std::vector<Message>& messages,
                      const std::vector<ToolSchema>& tools);

    void startRequestStreaming(const std::string& systemPrompt,
                               const std::vector<Message>& messages,
                               const std::vector<ToolSchema>& tools);

    std::vector<mpsc::AppCoreEvent> tick();
    void cancel();
    bool active() const { return m_active; }
    int timeoutMs() const;
    void setMockUrl(const std::string& url);
    const std::string& mockUrl() const { return m_baseUrl; }

private:
    struct EasyHandle {
        CURL* easy = nullptr;
        curl_slist* headers = nullptr;
        std::string requestBody;
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

    void xSetupCommon(CURL* curl, curl_slist*& headers, bool streaming);
    void xBuildPayload(json& payload,
                       const std::string& systemPrompt,
                       const std::vector<Message>& messages,
                       const std::vector<ToolSchema>& tools,
                       bool stream) const;
    void xUpdatePollInfo() const;
    void xProcessCompletion(CURL* easy, CURLcode result,
                            std::vector<mpsc::AppCoreEvent>& out);
};

} // namespace a0
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Public_API
        SR[startRequest]
        SRS[startRequestStreaming]
        TICK[tick]
        CANCEL[cancel]
    end

    subgraph Internals
        BUILD[xBuildPayload]
        SETUP[xSetupCommon]
        POLL[xUpdatePollInfo]
        COMPL[xProcessCompletion]
    end

    subgraph curl
        MULTI[curl_multi]
        EASY[CURL easy handle]
        FDSET[curl_multi_fdset]
        PERFORM[curl_multi_perform]
        INFO[curl_multi_info_read]
    end

    subgraph Decoder
        DEC[ResponseDecoder]
    end

    SR --> BUILD --> SETUP --> EASY
    SRS --> BUILD --> SETUP --> EASY
    EASY --> MULTI
    TICK --> PERFORM
    TICK --> INFO
    TICK --> POLL
    INFO --> COMPL
    COMPL -->|non-streaming| DEC
    TICK -->|streaming feed| DEC
    TICK -->|events| out[(AppCoreEvent vector)]
    CANCEL --> MULTI
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant C as Caller
    participant DP as DrivenProvider
    participant CURL as curl_multi
    participant DEC as ResponseDecoder

    C->>DP: startRequestStreaming(sysPrompt, msgs, tools)
    DP->>DP: xBuildPayload(payload)
    DP->>DP: xSetupCommon(curl, headers)
    DP->>CURL: curl_multi_add_handle()
    DP-->>C: (returns immediately)

    loop tick cycle
        C->>DP: tick()
        DP->>CURL: curl_multi_wait() — drives async DNS & I/O
        Note over DP,CURL: Zero-timeout poll of curl's internal fds.<br/>Required for async DNS resolver to complete.
        DP->>CURL: curl_multi_perform()
        CURL-->>DP: running handles
        DP->>CURL: curl_multi_info_read()
        CURL-->>DP: CURLMsg (done info)
        DP->>DP: xProcessCompletion()
        alt streaming
            DP->>DEC: feed(responseBody)
            DEC-->>DP: decoder events
        else non-streaming
            DP->>DEC: decodeJson(responseBody)
            DEC-->>DP: decoder events
        end
        DP-->>C: vector&lt;AppCoreEvent&gt;
    end

    C->>DP: cancel()
    DP->>CURL: curl_multi_remove_handle()
    DP->>CURL: curl_easy_cleanup()
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| Non-streaming request completes | HTTP 200, response body decoded into events |
| Streaming request emits tokens | tick() returns `LlmToken` events incrementally |
| Cancel in-flight request | active() returns false, no more events |
| HTTP error response | `Error` event with status code and body |
| Curl transport error | `Error` event with curl error string |
| timeoutMs() when idle | Returns -1 |
| timeoutMs() when active | Returns curl-derived timeout (≥1ms) |
| setMockUrl() changes endpoint | Subsequent requests go to mock URL |
| Multiple startRequest calls | Previous request cancelled, new one started |
| SSL skip for localhost | Verification disabled for localhost/127.0.0.1 |
