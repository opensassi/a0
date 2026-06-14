# EntryComponent Spec — New File Proposal

## §1. Overview

**Role:** Retained FTXUI Components for rendering conversation entries. Two concrete types — `TurnComponent` (represents one logical turn in the conversation) and `AssistantComponent` (represents one assistant message with its tool children). Both read from `MessageStore` and `StateManager`. The component tree is a Container::Vertical of TurnComponents, each containing an AssistantComponent with ToolCallComponent children.

**Source files:** `src/tui/entry_component.h`, `src/tui/entry_component.cpp`

**Dependencies:** `ftxui/component/component_base.hpp`, `ftxui/component/container.hpp`, `ftxui/dom/elements.hpp`, `src/tui/message_store.h`, `src/tui/state_manager.h`, `src/tui/tool_call_component.h`, `src/tui/styles.h`

**Lifecycle:**
1. Created by `MessagePanel::xMakeTurnComponent(turn_id)` factory
2. TurnComponent creates child AssistantComponent (and, in future, UserComponent)
3. AssistantComponent creates ToolCallComponent children based on `store_.assistant(id).tool_ids`
4. Added to MessagePanel's Container::Vertical
5. On Store updates: `xSyncComponents()` calls `Reconcile()` to add/remove tool children
6. Removed/destroyed when sync removes the turn from the tree

## §2. Component Specifications

```cpp
namespace a0::tui {

class TurnComponent : public ComponentBase {
public:
    TurnComponent(const std::string& turn_id,
                  MessageStore* store,
                  StateManager* stateMgr);
    ~TurnComponent() override;

private:
    Element OnRender() override;

    std::string m_turnId;
    MessageStore* m_store;
    StateManager* m_stateMgr;
    Container::Vertical m_container;

    void xRebuild();
};

class AssistantComponent : public ComponentBase {
public:
    AssistantComponent(const std::string& asst_id,
                       MessageStore* store,
                       StateManager* stateMgr);
    ~AssistantComponent() override;

    // Called by parent after Store updates
    void reconcile();

private:
    Element OnRender() override;

    std::string m_asstId;
    MessageStore* m_store;
    StateManager* m_stateMgr;
    Box m_box;                          // stable — never reallocated

    // Child tool call components
    std::vector<ftxui::Component> m_toolComponents;

    void xRebuildToolChildren();
};

} // namespace a0::tui
```

## §3-7

(Standard structure. Key tests: TurnComponent renders one turn, AssistantComponent renders role label + text + tool children, reconcile adds/removes ToolCallComponent children when Store's tool_ids change, Box member is stable across renders, nested component tree propagates events correctly.)
