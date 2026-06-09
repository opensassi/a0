# Technical Specification: LLM Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

The LLM sub-module owns all LLM provider abstractions and implementations. It is extracted from the monolithic `a0_lib` and contains the async curl_multi-based provider machinery plus the SSE/JSON response decoder.

**Source files:**
- `llm_provider.h` — abstract async LLM interface
- `driven_provider.h/.cpp` — curl_multi base implementation
- `deepseek_provider.h/.cpp` — DeepSeek-specific subclass
- `response_decoder.h/.cpp` — SSE/JSON response decoder

**Dependencies:** `shared_lib`, `libcurl`, `nlohmann_json`

**Namespace:** `a0`

---

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

class DrivenProvider : public LlmProvider {
    // curl_multi base, protected virtual hooks
};

class DeepSeekProvider : public DrivenProvider {
    // DeepSeek-specific config
};

class ResponseDecoder {
    // SSE/JSON decoder: feed(bytes) → events()
};

} // namespace a0
```

Public API is unchanged from the existing specification. All includes now resolve through shared_lib.

---

## 3. Build System

```cmake
add_library(llm_lib STATIC
    driven_provider.cpp
    deepseek_provider.cpp
    response_decoder.cpp
)
target_include_directories(llm_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llm_lib PUBLIC
    shared_lib
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
```

---

## 4. Testing

Tests are covered by existing test files with updated include paths and library targets. See `test_driven_core_persistence.cpp` and `test_deepseek_provider.cpp` (uncompiled).
