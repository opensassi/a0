# HandlerResult Spec

## 1. Overview

Lightweight return type for all C++ system tool handler functions. Carries the output string and an optional list of recommended tool names used for dynamic tool accumulation.

**Source file:** `src/shared/handler_results.h`

## 2. Component Specifications

```cpp
namespace a0 {

struct HandlerResult {
    std::string output;
    std::vector<std::string> recommendedTools;
};

}
```

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Definition
        HR[HandlerResult]
    end

    subgraph Fields
        OUT[output: string]
        REC[recommendedTools: vector<string>]
    end

    subgraph Consumers
        SH[system_handlers]
        SM[SkillManager]
        AC[AgentCore]
    end

    HR --> OUT
    HR --> REC
    SH -->|returns| HR
    SM -->|extracts| HR
    AC -->|uses recommendedTools| HR
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant H as Handler
    participant SM as SkillManager
    participant AC as AgentCore

    H->>SM: return HandlerResult{output, {tool1, tool2}}
    SM->>SM: extract .output for response
    SM->>AC: return HandlerResult with recommendations
    AC->>AC: xAccumulateTools(recommendedTools)
    Note over AC: Tools available for next LLM turn
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| Default construction | output.empty(), recommendedTools.empty() |
| Output-only result | HandlerResult{"ok"} — output populated, recommendedTools empty |
| Result with recommendations | HandlerResult{"plan", {"tool1", "tool2"}} — both fields populated |
