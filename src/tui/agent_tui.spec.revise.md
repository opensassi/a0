# AgentTui Spec — Revision Proposals

## Event Handlers Route to Store (Shim Phase)

### Revision 4.1 — AgentTui owns a MessageStore

**Section affected**: §2 Component Specifications, private members
**Original text**:
```cpp
    // --- Streaming state ---
    std::string m_streamingText;
    int m_assistantEntryIndex = -1;
    int64_t m_currentStreamId = 0;
    int m_currentRoundSeq = 0;
    bool m_hasActiveStream = false;
```
**Proposed change**: Add:
```cpp
    // --- Store (event projection) ---
    MessageStore m_store;
    StateManager m_stateMgr;
```
Retain existing streaming state members during shim phase (they're removed in the retained tree phase).
**Reason**: The shim phase adds Store projection alongside existing rendering logic. Both data paths run in parallel for validation.

### Revision 4.2 — xOn* handlers call store_.apply(event) first

**Section affected**: §2, xOn* method implementations
**Original text**: (each handler directly calls MessagePanel methods)
**Proposed change**: Add `m_store.apply(event)` as the first line of each event handler:
```
xOnLlmChunk(streamId, seq, text, isFinal) {
    m_store.apply(LlmChunk{streamId, seq, text, isFinal});  // NEW
    ...existing code unchanged...
}
```
**Reason**: The shim phase writes to Store alongside existing rendering logic. This validates Store projections before the rendering migration.

### Revision 4.3 — Event handlers take the full struct, not field-by-field

**Section affected**: §2, xOn* method signatures
**Original text**:
```cpp
    void xOnLlmChunk(int64_t streamId, int seq, const std::string& text, bool isFinal);
    void xOnToolStart(int64_t invocationId, const std::string& toolCallId,
                      const std::string& toolName, const std::string& arguments);
```
**Proposed change**: Replace with struct-based signatures (post-shim, when only Store remains):
```cpp
    void xOnLlmChunk(const mpsc::LlmChunk& ev);
    void xOnToolStart(const mpsc::ToolStart& ev);
```
**Reason**: Once the MessagePanel direct calls are removed, handlers become one-liners: `m_store.apply(ev)`. Taking the full struct eliminates field unpacking.

### Revision 4.4 — Remove streaming cursor members (retained tree phase)

**Section affected**: §2, private members
**Proposed change**: Delete `m_streamingText`, `m_assistantEntryIndex`, `m_hasActiveStream`. These are replaced by Store's identity maps.
**Reason**: The Store's cursor-free event processing eliminates the need for the TUI to track which assistant is "active." Each event carries its own context.

### Revision 4.5 — Data flow diagram update

**Section affected**: §4 Data Flow, sequence diagram
**Original text**:
```
alt LlmChunk
    AT->>MP: appendOrUpdateAssistantText(idx, text)
```
**Proposed change** (shim phase): Add Store projection line:
```
alt LlmChunk
    AT->>Store: apply(LlmChunk{...})
    AT->>MP: appendOrUpdateAssistantText(idx, text)   (PHASED OUT)
```
**Reason**: The shim shows both paths operating in parallel.

### Revision 4.6 — Update Testing Requirements

**Section affected**: §5 Testing Requirements
**Proposed change**: Add test:
| Test | Verification |
|------|-------------|
| store_.apply(LlmChunk) after drainEvents | Store.turn_order() reflects projected data |
| store_.apply(ToolStart) with parentStreamId | Store.tool_for_invocation() returns correct parent |
