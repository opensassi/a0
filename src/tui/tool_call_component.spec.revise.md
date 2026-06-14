# ToolCallComponent Spec — New File Proposal

## §1. Overview

**Role:** Retained FTXUI Component for rendering and interacting with a single tool call block. Has its own `Box box_` member (stable, never reallocated) used with `ftxui::reflect()` for mouse hit-testing. Reads data from `MessageStore` and state from `StateManager`. Supports click-to-collapse, lazy data loading on expand, and streaming output updates.

**Source files:** `src/tui/tool_call_component.h`, `src/tui/tool_call_component.cpp`

**Dependencies:** `ftxui/component/component_base.hpp`, `ftxui/dom/elements.hpp`, `src/tui/message_store.h`, `src/tui/state_manager.h`, `src/tui/styles.h`

**Lifecycle:**
1. Created by `MessagePanel::xMakeToolCallComponent(tool_id)` factory
2. Added as child of an `AssistantComponent`'s Container::Vertical
3. Each render: reads `Store::tool(tool_id_)` + `StateManager::is_collapsed(tool_id_)`
4. Click toggles collapse via `StateManager::toggle_collapsed(tool_id_)`
5. On expand with unloaded data: triggers LoadResource request
6. Removed/destroyed when parent sync removes the tool from the component tree

## §2. Component Specifications

```cpp
namespace a0::tui {

class ToolCallComponent : public ComponentBase {
public:
    ToolCallComponent(const std::string& tool_id,
                      MessageStore* store,
                      StateManager* stateMgr);
    ~ToolCallComponent() override;

    // Event handling
    bool OnEvent(Event event) override;

    // Accessors
    const std::string& tool_id() const { return m_toolId; }

private:
    Element OnRender() override;

    std::string m_toolId;
    MessageStore* m_store;
    StateManager* m_stateMgr;
    Box m_box;                      // stable — never reallocated

    // Lazy loading state
    bool m_dataRequested = false;

    Element xRenderHeader();
    Element xRenderBody();
};

} // namespace a0::tui
```

## §3-7

(Standard structure. Key tests: OnRender reads from Store, OnEvent toggles collapsed via StateManager, Box member is stable across renders, data request on expand, header always visible in collapsed state.)
