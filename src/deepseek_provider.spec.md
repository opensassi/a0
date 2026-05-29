# DeepSeekProvider Spec

## 1. Overview

DeepSeekProvider implements InferenceProvider by making HTTPS POST requests to the DeepSeek Chat Completions API via libcurl. It constructs a JSON payload with system and user messages, sends it to `https://api.deepseek.com/v1/chat/completions`, and extracts the assistant's reply from `choices[0].message.content`. SSL verification is skipped when the base URL contains `localhost` or `127.0.0.1` (for test mocking). It is consumed by DefaultAgentCore during skill inference and prompt expansion.

## 2. Component Specifications

```cpp
class DeepSeekProvider : public InferenceProvider {
public:
    /// \param apiKey  DeepSeek API key (sent as Bearer token)
    /// \param model   Model identifier (default: "deepseek-chat")
    DeepSeekProvider(const std::string& apiKey,
                     const std::string& model = "deepseek-chat");

    /// \param systemPrompt  System-level instruction (may be empty)
    /// \param userPrompt    User message content
    /// \retval Assistant response text from choices[0].message.content
    /// \retval Empty string on HTTP/network/parse error
    std::string complete(const std::string& systemPrompt,
                         const std::string& userPrompt) override;

    /// \param url  Override base URL (used for mock/test servers)
    void setMockUrl(const std::string& url) override;

private:
    std::string m_apiKey;
    std::string m_model;
    std::string m_baseUrl;  // Default: https://api.deepseek.com/v1/chat/completions
};
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Provider
        DSP[DeepSeekProvider]
        KEY[m_apiKey]
        MODEL[m_model]
        URL[m_baseUrl]
    end

    AC[DefaultAgentCore] --> DSP
    DSP --> CURL[libcurl]
    CURL --> API[DeepSeek API / Mock Server]

    subgraph Request
        PAYLOAD[POST JSON body]
        AUTH[Authorization: Bearer key]
        HEADERS[Content-Type: application/json]
    end

    DSP --> PAYLOAD
    DSP --> AUTH
    DSP --> HEADERS
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant AC as DefaultAgentCore
    participant DSP as DeepSeekProvider
    participant CURL as libcurl
    participant API as DeepSeek API

    AC->>DSP: complete(systemPrompt, userPrompt)
    DSP->>DSP: build JSON payload
    DSP->>CURL: curl_easy_init()
    CURL-->>DSP: CURL* handle
    DSP->>CURL: set URL (m_baseUrl)
    DSP->>CURL: set headers (Content-Type + Authorization)
    DSP->>CURL: set POST body (JSON dump)
    DSP->>CURL: set WRITEFUNCTION / WRITEDATA
    alt localhost URL
        DSP->>CURL: SSL_VERIFYPEER = 0
        DSP->>CURL: SSL_VERIFYHOST = 0
    end
    DSP->>CURL: curl_easy_perform()
    CURL->>API: HTTPS POST
    API-->>CURL: JSON response
    CURL-->>DSP: response string
    DSP->>DSP: parse JSON
    DSP->>DSP: extract choices[0].message.content
    DSP-->>AC: response text

    Note over DSP: On curl/parse error → returns ""
```

## 5. Error Handling

| Condition | Behaviour |
|-----------|-----------|
| `curl_easy_init()` returns nullptr | Returns empty string |
| Network failure / timeout (30s) | `curl_easy_perform` returns non-CURLE_OK; returns empty string |
| API returns JSON with an `"error"` field | Returns empty string |
| Response JSON missing `choices[0].message.content` | JSON parse exception caught; returns empty string |
| Response body is not valid JSON | `json::parse` exception caught; returns empty string |
| Empty apiKey provided | Request sent with `"Authorization: Bearer "` — API returns 401; returns empty string |

## 6. Edge Cases

| Case | Behaviour |
|------|-----------|
| Empty `systemPrompt` | `messages` array contains only the user message |
| Empty `userPrompt` | Request sent with empty user content |
| Very long prompts (>100K tokens) | Sent as-is; API may truncate or reject |
| Mock URL with trailing slash | Sent as-is to the mock server (no normalization) |
| Mock URL on `localhost:PORT` | SSL verification is skipped automatically |
| API key with special characters | Passed verbatim in the Bearer header |
| Multiple `choices` in response | Only `choices[0]` is used |
| Concurrent calls from different threads | Not thread-safe (shared curl handle) |
| `setMockUrl` after construction | `m_baseUrl` is updated; next `complete()` call uses it |

## 7. Testing Requirements

| Method | Test | Input | Expected |
|--------|------|-------|----------|
| `complete` | Successful response | Valid prompts, mock returns `{"choices":[{"message":{"content":"ok"}}]}` | Returns `"ok"` |
| `complete` | API error field | Mock returns `{"error":"rate limit"}` | Returns `""` |
| `complete` | Network timeout | Mock hangs for 31s | Returns `""` (curl 30s timeout) |
| `complete` | Invalid JSON response | Mock returns `not json` | Returns `""` |
| `complete` | Empty system prompt | `systemPrompt=""`, mock returns valid content | Returns content (system message omitted from array) |
| `complete` | Empty user prompt | `userPrompt=""`, mock returns valid content | Returns content |
| `complete` | curl init failure* | Force curl_easy_init to return nullptr | Returns `""` |
| `complete` | Missing `choices` key | Mock returns `{}` | Returns `""` (parse exception) |
| `complete` | Missing `message.content` | Mock returns `{"choices":[{}]}` | Returns `""` (parse exception) |
| `complete` | localhost mock URL | `setMockUrl("http://localhost:8080/...")` | SSL verification skipped, request succeeds |
| `setMockUrl` | Override URL | `"http://127.0.0.1:9999/v1/chat"` | Next `complete()` uses the new URL |
| `complete` | API key with special chars | Key `"abc!@#"` | Request sent with exact header `Authorization: Bearer abc!@#` |

\* *Hard to unit test without dependency injection; acceptance via code review.*
