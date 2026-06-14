# MessageStore Spec — New File Proposal

## §1. Overview

**Role:** Event projection store for the TUI. Receives every `AppCoreEvent` from the MPSC channel and projects it into queryable data structures. Maintains identity maps (`streamId → assistant_id`, `invocationId → tool_id`) that allow components to look up data by event-carried identifiers. Tracks which data_ids changed since the last check (`dirty_ids_`) for efficient re-rendering.

**Source files:** `src/tui/message_store.h`, `src/tui/message_store.cpp`

**Dependencies:** `src/shared/mpsc.h` (event types)

**Lifecycle:**
1. Constructed at AgentTui creation time
2. `apply(event)` called for each MPSC event in order — no cursor, no hidden state
3. Components read projected data via `turn()`, `assistant()`, `tool()` accessors
4. `consume_dirty()` returns the set of changed data_ids since last check
5. `turn_order()` returns the canonical ordered list of turn IDs

## §2. Component Specifications

```cpp
namespace a0::tui {

struct TurnData {
    std::string id;                        // "turn_1"
    std::string assistant_id;              // "asst_3"
    int64_t turnSeq;
};

struct AssistantData {
    std::string id;                        // "asst_3"
    std::string text;                      // accumulated via LlmChunk
    bool streaming;
    std::vector<std::string> tool_ids;     // ["tc_5", "tc_6"]
};

struct ToolData {
    std::string id;                        // "tc_5"
    int64_t invocationId;
    std::string name;
    std::string args;
    std::string output;                    // accumulated via ToolChunk
    bool completed;
    int exitCode;
    int64_t totalBytes;
};

class MessageStore {
public:
    MessageStore();

    // Process one event — explicit references only, no cursor
    void apply(const mpsc::LlmStart& ev);
    void apply(const mpsc::LlmChunk& ev);
    void apply(const mpsc::LlmComplete& ev);
    void apply(const mpsc::ToolStart& ev);     // uses parentStreamId
    void apply(const mpsc::ToolChunk& ev);     // uses invocationId
    void apply(const mpsc::ToolEnd& ev);       // uses invocationId
    void apply(const mpsc::Complete& ev);      // uses turnSeq
    void apply(const mpsc::Error& ev);
    void apply(const mpsc::SessionHistory& ev); // bulk load

    // Queries
    const std::vector<std::string>& turn_order() const;
    const TurnData& turn(const std::string& id) const;
    const AssistantData& assistant(const std::string& id) const;
    const ToolData& tool(const std::string& id) const;

    // Identity maps (event-carried ID → internal ID)
    std::string assistant_for_stream(int64_t streamId) const;
    std::string tool_for_invocation(int64_t invocationId) const;

    // Dirty tracking — which ids changed since last consume
    std::unordered_set<std::string> consume_dirty();

    void clear();

private:
    // Identity maps
    std::unordered_map<int64_t, std::string> m_streamToAsst;
    std::unordered_map<int64_t, std::string> m_invocationToTool;

    // Projected data
    std::vector<std::string> m_turnOrder;
    std::unordered_map<std::string, TurnData> m_turns;
    std::unordered_map<std::string, AssistantData> m_assistants;
    std::unordered_map<std::string, ToolData> m_tools;

    // Dirty set
    std::unordered_set<std::string> m_dirty;

    // Cursor for tool start ordering within assistant
    int m_nextToolSeq = 0;
};

} // namespace a0::tui
```

## §3-7

(Follow standard 7-section structure. Architecture diagram shows Store between Event stream and Component tree. Data flow shows each event → projection → dirty set. Testing covers every apply() method, identity map lookups, dirty consumption, and bulk session history replay.)
