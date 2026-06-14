# MessagePanel Spec — Revision Proposals

## Complete Rewrite for Retained Tree Architecture

### Revision 5.1 — Remove ToolHit struct and entryBoxes/toolHits vectors

**Section affected**: §2 Component Specifications, `ToolHit` struct and `Impl` class
**Original text**:
```cpp
struct ToolHit {
    int entryIdx;
    int childIdx;
    ftxui::Box box;
};

class MessagePanel::Impl {
public:
    std::vector<MessageEntry> entries;
    ftxui::Component renderer;
    int focusIndex = 0;
    bool autoScroll = true;
    std::vector<ftxui::Box> entryBoxes;
    std::vector<ToolHit> toolHits;
};
```
**Proposed change**: Replace with retained tree architecture:
```cpp
class MessagePanel::Impl {
public:
    MessageStore* store = nullptr;                  // injected, not owned
    StateManager* stateMgr = nullptr;               // injected, not owned
    ftxui::Component container;                     // Container::Vertical
    ftxui::Component outer;                         // CatchEvent + Renderer wrapper
    int focusIndex = 0;
    bool autoScroll = true;

    // Component registry — maps data_id → component for targeted updates
    std::unordered_map<std::string, ftxui::Component> comp_by_id;
};
```
**Reason**: Eliminates the vector reallocation bug permanently. Each component owns its own `Box` member. No shared mutable vectors between render and event phases. No more `ToolHit` or `entryBoxes` — mouse hit-testing is per-component via `box_.Contain()`.

### Revision 5.2 — Remove immediate-mode render methods

**Section affected**: §2, private methods
**Original text**:
```cpp
    ftxui::Element xRenderEntry(int entryIdx);
    ftxui::Element xRenderAssistant(int entryIdx);
    ftxui::Element xRenderToolBlock(const MessageEntry& entry) const;
    ftxui::Element xRenderStreamingPlaceholder(const MessageEntry& entry) const;
```
**Proposed change**: Replace with:
```cpp
    // Component factories (create retained Components from Store data)
    ftxui::Component xMakeTurnComponent(const std::string& turn_id);
    ftxui::Component xMakeAssistantComponent(const std::string& asst_id);
    ftxui::Component xMakeToolCallComponent(const std::string& tool_id);

    // Sync — reconcile component tree with Store after updates
    void xSyncComponents();
```
**Reason**: Rendering is now per-component, not per-entry-in-a-loop. Factories create retained Components; `xSyncComponents` reconciles the component tree with Store's `turn_order()`.

### Revision 5.3 — Public API simplification

**Section affected**: §2, public methods
**Original text**:
```cpp
    int append(const MessageEntry& entry);
    void clear();
    int beginAssistant();
    int appendOrUpdateAssistantText(int asstIdx, const std::string& text);
    int endCurrentAssistantText(int asstIdx);
    int appendAssistantTool(int asstIdx, ...);
    int updateLastAssistantTool(int asstIdx, ...);
    int updateLastAssistantToolOutput(int asstIdx, ...);
    int finalizeAssistant(int asstIdx);
    int loadHistory(const std::vector<::a0::mpsc::SessionMessage>& messages);
```
**Proposed change**: Simplify to:
```cpp
    ftxui::Component component() const;
    void setStore(MessageStore* store);
    void setStateManager(StateManager* mgr);
    void sync();            // reconcile component tree with Store
    void clear();
    size_t count() const;
    void scrollUp(int n = 1);
    void scrollDown(int n = 1);
    void scrollToTop();
    void scrollToBottom();
    bool isAtBottom() const;
```
**Reason**: The old imperative API (append, update, finalize) was a workaround for immediate-mode rendering. With a retained tree and Store, the component tree is driven by Store data. The only update interface needed is `sync()` to reconcile after Store changes.

### Revision 5.4 — MessageEntry struct removed from MessagePanel

**Section affected**: §2, `MessageEntry` struct
**Original text**: (struct defined in message_panel.h, used as the data model)
**Proposed change**: Move `MessageEntry` to a shared location or eliminate it entirely in favor of Store's projected data types (`TurnData`, `AssistantData`, `ToolData`). MessagePanel no longer owns or stores a vector of MessageEntry.
**Reason**: Data moves to Store. The panel is purely a renderer that reads from Store and StateManager.

### Revision 5.5 — Remove xRenderEntry dispatch on MessageRole

**Section affected**: §3 Architecture Diagram
**Original text**:
```
    subgraph "Render Methods"
        RE[xRenderEntry]
        RA[xRenderAssistant]
        RT[xRenderToolBlock]
        RS[xRenderStreamingPlaceholder]
    end
```
**Proposed change**: Replace with:
```
    subgraph "Component Factories"
        MTC[xMakeTurnComponent]
        MAC[xMakeAssistantComponent]
        MTC[xMakeToolCallComponent]
        SYNC[xSyncComponents]
    end
```
**Reason**: Role-based dispatch moves to the factory functions. Each factory creates the appropriate Component type based on Store data.

### Revision 5.6 — Update Testing Requirements

**Section affected**: §5 Testing Requirements
**Proposed change**: Replace old method-level tests with:
| Test | Verification |
|------|-------------|
| sync() empty store | container.ChildCount() == 0 |
| sync() one turn | container.ChildCount() == 1 |
| sync() two turns | container.ChildCount() == 2 |
| setStore + sync + setStore(new) | container rebuilt from new store |
| scrollUp/down at boundaries | focusIndex clamped correctly |
| isAtBottom after new entry | returns true with autoScroll |
| Component registry lookup | comp_by_id[turn_id] returns non-null |
