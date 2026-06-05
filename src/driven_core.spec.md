# DrivenCore Spec

## 1. Overview

State-machine driven agent core that replaces the forked-loop implementation. Transitions through `Idle → AwaitingLlm → ExecutingTools → Idle` on each goal. `submitGoal()` starts an LLM request, `tick()` drives the provider and tool execution, and `cancel()` resets to idle. `runSync()` provides a synchronous convenience wrapper for headless/CLI use.

Uses `LlmProvider*` instead of a concrete provider — works with any LLM backend.

**Source files:** `src/driven_core.h/.cpp`

**Dependencies:** `llm_provider.h`, `mpsc.h`, `agent_interfaces.h`, `skills/skills.h`, `persistence/persistence_store.h`, `base_prompt.h`, `dependency_graph.h`

## 2. Component Specifications

```cpp
namespace a0 {

class DrivenCore {
public:
    DrivenCore(LlmProvider* provider,
               a0::skills::SkillManager* skillMgr,
               a0::persistence::PersistenceStore* persistence = nullptr);

    void submitGoal(const std::string& goal);
    std::string runSync(const std::string& goal);
    std::vector<mpsc::AppCoreEvent> tick();
    bool idle() const { return m_state == CoreState::Idle; }
    void cancel();

    void setSession(int64_t sessionDbId, const std::string& sessionUuid);
    int64_t sessionDbId() const { return m_sessionDbId; }
    const std::string& lastResult() const { return m_lastResult; }

private:
    enum class CoreState { Idle, AwaitingLlm, ExecutingTools };

    CoreState m_state = CoreState::Idle;
    LlmProvider* m_provider;
    a0::skills::SkillManager* m_skillMgr;
    a0::persistence::PersistenceStore* m_persistence;

    std::string m_lastResult;
    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    int64_t m_subSessionId = 0;
    int m_seq = 0;
    int m_turnCount = 0;

    std::vector<Message> m_messages;
    std::vector<ToolSchema> m_toolSchemas;
    std::vector<ToolSchema> m_emptySchemas;
    std::unordered_map<std::string, std::string> m_dispatch;
    std::string m_accumText;

    struct PendingToolCall {
        std::string id;
        std::string name;
        json arguments;
    };
    std::vector<PendingToolCall> m_pendingToolCalls;

    static constexpr int MAX_TURNS = 25;

    void xBuildInitialMessages(const std::string& goal);
    void xBuildToolSchemas();
    void xStartLlmRequest(bool includeTools = false);
    void xHandleLlmEvents(const std::vector<mpsc::AppCoreEvent>& events);
    void xExecuteTools();
    void xFinishGoal(const std::string& text);
    void xFailGoal(const std::string& error);
    void xPersistMessage(const std::string& role, const std::string& content,
                         const std::string& toolCallId = "",
                         const std::vector<ToolCall>& toolCalls = {});
};

} // namespace a0
```

Key differences from the legacy `AgentCore`:
- No `tools_for_prompt` call in `xBuildInitialMessages` — the base prompt lists all available tools directly
- Base prompt loaded from disk via `buildBasePrompt()` instead of `getPrompt("system-base")`
- First LLM request uses `includeTools=false` (system prompt lists tools; tool schemas are sent on follow-up requests)
- All tool execution goes through `DependencyGraph::buildBatches`/`executeBatches`

## 3. Architecture Diagram

```mermaid
graph TB
    subgraph Public_API
        SG[submitGoal]
        RS[runSync]
        TICK[tick]
        CANCEL[cancel]
    end

    subgraph State_Machine
        IDLE[Idle]
        LLM[AwaitingLlm]
        TOOLS[ExecutingTools]
    end

    subgraph Internal_Methods
        BUILD[xBuildInitialMessages]
        SCHEMA[xBuildToolSchemas]
        START[xStartLlmRequest]
        HANDLE[xHandleLlmEvents]
        EXEC[xExecuteTools]
        FINISH[xFinishGoal]
        FAIL[xFailGoal]
    end

    subgraph Dependencies
        PROV[LlmProvider]
        SKILL[SkillManager]
        PERS[PersistenceStore]
        BASE[buildBasePrompt]
    end

    SG --> BUILD --> SCHEMA --> START
    START --> PROV
    START --> BASE
    RS --> SG
    TICK --> LLM
    LLM -->|tick provider| HANDLE
    HANDLE -->|Complete + tools| TOOLS
    HANDLE -->|Complete + text| FINISH
    HANDLE -->|Error| FAIL
    TOOLS --> EXEC
    EXEC --> SKILL
    EXEC -->|next turn| START
    EXEC -->|max turns| FAIL
    FINISH --> IDLE
    FAIL --> IDLE
    CANCEL --> IDLE
    FINISH --> PERS
    FAIL --> PERS
```

## 4. Data Flow

```mermaid
sequenceDiagram
    participant C as Caller
    participant DC as DrivenCore
    participant PR as LlmProvider
    participant SK as SkillManager
    participant PT as PersistenceStore

    C->>DC: submitGoal("write a test")
    DC->>DC: xBuildInitialMessages()
    DC->>DC: xBuildToolSchemas()
    DC->>PR: startRequestStreaming(...)
    DC->>DC: m_state = AwaitingLlm

    loop tick loop
        C->>DC: tick()
        DC->>PR: tick()
        PR-->>DC: events (tokens, tool_calls, complete)
        DC->>DC: xHandleLlmEvents()
        DC-->>C: events (forwarded)
    end

    Note over DC: LLM returns tool_calls
    DC->>DC: m_state = ExecutingTools

    DC->>DC: xExecuteTools()
    DC->>SK: DependencyGraph batch execution
    SK-->>DC: tool results
    DC->>DC: xStartLlmRequest(includeTools=true)
    DC->>PR: startRequestStreaming(...)

    Note over DC: LLM returns text
    DC->>DC: xFinishGoal(text)
    DC->>DC: m_state = Idle
```

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| submitGoal from idle | Transitions to AwaitingLlm, starts request |
| runSync completes | Returns final output text, core idle |
| tick in Idle state | Returns empty vector |
| tick in AwaitingLlm | Forwards provider events |
| LLM returns text only | Calls xFinishGoal, transitions to Idle |
| LLM returns tool calls | Transitions to ExecutingTools |
| Tool executes successfully | Result added to messages, next LLM started |
| Max tool call turns exceeded | xFailGoal with error message |
| cancel() from any state | Provider cancelled, state = Idle, state cleared |
| setSession before submit | User message persisted to session |
| User message persisted | `loadMessages(sessionId)` contains user role |
| Assistant text persisted | `loadMessages(sessionId)` contains assistant role |
| No persistence without session | `loadMessages(0)` returns empty |
| Session switch | Messages go to correct session after setSession call |
| UTF-8 sanitization of tool output | Invalid sequences replaced with `?` |
