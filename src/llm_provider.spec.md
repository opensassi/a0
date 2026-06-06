# LlmProvider Spec

## 1. Overview

Abstract async LLM provider interface. Defines the contract that all LLM providers must implement for use with `DrivenCore`. Non-blocking `startRequest()` / `tick()` API designed for event-loop integration. Implementations provide provider-specific API format, auth, and transport.

**Source file:** `src/llm_provider.h`

**Dependencies:** `agent_interfaces.h`, `mpsc.h`

## 2. Component Specifications

```cpp
namespace a0 {

class LlmProvider {
public:
    virtual ~LlmProvider() = default;

    virtual void startRequest(const std::string& systemPrompt,
                              const std::vector<Message>& messages,
                              const std::vector<ToolSchema>& tools) = 0;

    virtual void startRequestStreaming(const std::string& systemPrompt,
                                       const std::vector<Message>& messages,
                                       const std::vector<ToolSchema>& tools) = 0;

    virtual std::vector<mpsc::AppCoreEvent> tick() = 0;
    virtual void cancel() = 0;
    virtual bool active() const = 0;
    virtual int timeoutMs() const = 0;
    virtual void setMockUrl(const std::string& url) = 0;
};

} // namespace a0
```

## 3. Implementation Hierarchy

```mermaid
graph TB
    LLM[LlmProvider — pure virtual]
    DP[DrivenProvider — curl_multi machinery]
    DS[DeepSeekProvider]
    OA[OpenAiProvider (future)]
    AN[AnthropicProvider (future)]

    LLM --> DP
    DP --> DS
    DP --> OA
    DP --> AN
```

## 4. Lifecycle

```
Construct → startRequest() / startRequestStreaming() → tick()* until complete → startRequest() again
                                              ↘ cancel() → idle
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| Mock provider returns events | tick() returns pre-programmed AppCoreEvent vector |
| startRequest sets active state | active() returns true |
| cancel resets state | active() returns false after cancel |
| Multiple startRequest calls | Previous implicitly cancelled |

## 6. Implementation Notes

- `DrivenCore` depends on `LlmProvider*`, not on any concrete implementation
- `DrivenProvider` provides the universal `curl_multi` machinery as a base class
- Subclasses (e.g. `DeepSeekProvider`) override `xBuildPayload` + `xAddAuth` for provider-specific format and auth
- `InferenceProvider` (synchronous, deprecated) has been deleted — `LlmProvider` is the only provider interface
