# Root Technical Specification — Revision Proposals

## TUI Architecture Update

### Revision 7.1 — Add new TUI components to Module Reference

**Section affected**: §1, Module Reference table
**Original text**: (existing TUI row)
```
| 10 | **tui** | `src/tui/` | AgentTui, ..., Clipboard | `src/tui/technical-specification.md` |
```
**Proposed change**: Add new files to the TUI row or note them as new sub-module entries:
- `MessageStore` — event projection store (new)
- `StateManager` — UI state preferences with global + per-item tiers (new)
- `TurnComponent` — retained turn-level component (new)
- `ToolCallComponent` — retained tool block component (new)
**Reason**: New source files created for retained tree architecture.

### Revision 7.2 — Update TUI sub-module overview (§2.10)

**Section affected**: §2.10, TUI sub-module component list
**Original text**: (lists AgentTui, InputPanel, MessagePanel, MarkdownRenderer, StatusBar, DialogManager, Clipboard, Styles)
**Proposed change**: Add MessageStore and StateManager to the list. Remove deprecated role descriptions for MessagePanel's immediate-mode rendering.
**Reason**: Architecture shift adds new data management components.

### Revision 7.3 — Remove ToolHit from shared data structures

**Section affected**: §2.10 (if ToolHit was referenced in the root spec as a shared type)
**Original text**: `ToolHit` struct if referenced.
**Proposed change**: Remove any reference to `ToolHit`. It no longer exists.
**Reason**: Retained tree eliminates the need for a separate hit-testing struct.

### Revision 7.4 — Update concurrency model to note cursor elimination

**Section affected**: §3 System Architecture / data flow dependencies
**Proposed change**: Add a note: "Events carry explicit parent references (`parentStreamId`, `turnSeq`). The TUI consumer no longer maintains an implicit cursor (`m_assistantEntryIndex`) to establish parent-child relationships."
**Reason**: Event enrichment eliminates the implicit cursor in the consumer.

### Revision 7.5 — New spec file declarations

**Section affected**: §2.10 (or a new sub-section)
**Proposed change**: Add brief role descriptions for the new spec files:
- `src/tui/message_store.spec.md` — MessageStore: event projection, identity maps, dirty tracking
- `src/tui/state_manager.spec.md` — StateManager: collapsed/expanded global defaults with per-item overrides
- `src/tui/tool_call_component.spec.md` — ToolCallComponent: retained component with stable Box, click-to-collapse, lazy loading
- `src/tui/entry_component.spec.md` — TurnComponent/AssistantComponent: retained conversation turn and assistant message components
**Reason**: New files need root-level cross-references.
