# DeepSeekProvider Spec

## 1. Overview

Concrete LLM provider for DeepSeek's API. Inherits from `DrivenProvider` which provides the universal `curl_multi` machinery. Implements `xBuildPayload()` (OpenAI-compatible JSON) and `xAddAuth()` (Bearer token). API key resolved from constructor argument first, falling back to `DEEPSEEK_API_KEY` env var.

**Source files:** `src/llm/deepseek_provider.h/.cpp`

**Dependencies:** `llm/driven_provider.h`, `libcurl`, `nlohmann/json`

## 2. Component Specifications

```cpp
namespace a0 {

class DeepSeekProvider : public DrivenProvider {
public:
    explicit DeepSeekProvider(const std::string& apiKey = "",
                              const std::string& model = "deepseek-chat");

protected:
    void xBuildPayload(json& payload,
                       const std::string& systemPrompt,
                       const std::vector<Message>& messages,
                       const std::vector<ToolSchema>& tools,
                       bool stream) const override;
    void xAddAuth(curl_slist*& headers) override;
};

} // namespace a0
```

## 3. Architecture Diagram

```mermaid
graph TB
    LLM[LlmProvider — abstract]
    DP[DrivenProvider — curl_multi]
    DS[DeepSeekProvider]
    KEY[DEEPSEEK_API_KEY env]
    URL[https://api.deepseek.com/v1/chat/completions]

    LLM --> DP
    DP --> DS
    DS --> KEY
    DS --> URL
    DS --> CURL[libcurl]
    CURL --> API[DeepSeek API / Mock Server]
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant C as DrivenCore
    participant DS as DeepSeekProvider
    participant DP as DrivenProvider (base)
    participant CURL as curl_multi

    C->>DS: startRequestStreaming(sys, msgs, tools)
    DS->>DP: xBuildPayload → OpenAI-compatible JSON
    DS->>DP: xAddAuth → Bearer header
    DP->>CURL: curl_multi_add_handle
    CURL-->>C: events via tick()
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| Constructor with explicit apiKey | m_baseUrl set to DeepSeek URL, apiKey used |
| Constructor with empty apiKey | DEEPSEEK_API_KEY env var checked |
| xBuildPayload | Produces valid OpenAI-compatible JSON with system prompt, messages, tools |
| xAddAuth | Adds Authorization: Bearer <key> to curl headers |
