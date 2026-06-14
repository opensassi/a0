# TUI Refactor: Retained Component Tree Architecture

## Overview

Refactor the TUI rendering layer from immediate-mode (rebuild Element tree from scratch every frame via a Renderer lambda) to a retained component tree (persistent `ComponentBase` subclasses with stable `Box` members). Introduce a `MessageStore` event projection layer and `StateManager` for UI preferences. Enrich MPSC events with explicit parent references (`parentStreamId`, `turnSeq`) to eliminate the implicit cursor.

## Design Principles

- **Test-driven**: Every change starts with tests. Stubs are written first, tests verify they fail, implementation makes them pass.
- **Spec-authority**: `.spec.md` files define the correct behavior. If a test contradicts the spec, the spec wins. If you believe the spec is wrong, notify the user.
- **Incremental shim**: The Store is integrated alongside existing code first (write-only), then the rendering is migrated piece by piece.
- **Single atomic change per phase**: One file modified, one set of tests, one verify cycle.

## Implementation Phases

---

### Phase 1: Event Enrichment — `src/shared/mpsc.h`

**Spec**: `src/shared/mpsc.spec.md`
**Revision**: `src/shared/mpsc.spec.revise.md` (Rev 1.1, 1.2, 1.3)

**Changes**:
- Add `int64_t parentStreamId` field to `ToolStart` struct
- Add `int64_t turnSeq` field to `Complete` struct

**Files to modify**: `src/shared/mpsc.h`

**TDD steps**:

1. **Stubs**: No stubs needed — this is a struct field addition. Update the struct definitions in `mpsc.h`.
2. **Tests**: `test/unit/test_mpsc.cpp` (or similar). Add tests that:
   - Construct a `ToolStart` with `parentStreamId` and verify the field round-trips
   - Construct a `Complete` with `turnSeq` and verify the field round-trips
   - Verify that `AppCoreEvent` variant holds the enriched types correctly
3. **Verify failing**: Run the MPSC test, confirm existing tests still pass, new tests pass (these are struct field additions — no logic changes, tests should pass immediately).
4. **Implement**: Update the struct definitions. No logic changes needed.
5. **Verify passing**: Run all MPSC tests.

**Rollback criteria**: If any existing test fails, revert the field addition and check the `AppCoreEvent` variant visitor for implicit dependencies on struct layout.

---

### Phase 2: Event Enrichment — `src/core/driven_core.cpp`

**Spec**: `src/core/driven_core.spec.md`
**Revision**: `src/core/driven_core.spec.revise.md` (Rev 3.1, 3.2, 3.3)

**Changes**:
- Add `int m_turnSeq = 0` member to `DrivenCore`
- Increment `m_turnSeq` on each `submitGoal()` call
- Emit `Complete{..., m_turnSeq, ...}` in `xFinishGoal` and `xFailGoal`

**Files to modify**: `src/core/driven_core.h`, `src/core/driven_core.cpp`

**TDD steps**:

1. **Stubs**: Add `m_turnSeq` to header, increment in `submitGoal()`.
2. **Tests**: `test/unit/test_driven_core_persistence.cpp`. Add tests:
   - Submit a goal → verify `Complete.turnSeq == 1`
   - Submit a second goal → verify `Complete.turnSeq == 2`
   - Submit goal, cancel, submit again → verify `turnSeq == 2` (not reset)
3. **Verify failing**: Run the core test. The `Complete` event emission uses the old struct (no turnSeq). Tests will fail to compile or the field won't exist.
4. **Implement**: Wire `m_turnSeq` through `xFinishGoal` and `xFailGoal`.
5. **Verify passing**: All core tests pass.

---

### Phase 3: Event Enrichment — `src/llm/response_decoder.cpp`

**Spec**: `src/llm/response_decoder.spec.md`
**Revision**: `src/llm/response_decoder.spec.revise.md` (Rev 2.1, 2.2, 2.3)

**Changes**:
- When emitting `ToolStart` events in `xProcessJsonChunk`, set `parentStreamId` from the decoder's active `m_streamId`

**Files to modify**: `src/llm/response_decoder.cpp`

**TDD steps**:

1. **Stubs**: No structural changes needed. `m_streamId` is already available at the point where `ToolStart` is emitted.
2. **Tests**: `test/unit/test_response_decoder.cpp` (or `test_deepseek_provider.cpp`). Add tests:
   - Feed SSE data with tool calls → verify `ToolStart.parentStreamId == decoder.streamId()`
   - Feed JSON non-streaming with tool calls → verify `ToolStart.parentStreamId == streamId`
3. **Verify failing**: The emitted `ToolStart` has `parentStreamId = 0` (default-initialized). Test asserts non-zero.
4. **Implement**: Set `parentStreamId = m_streamId` when pushing `ToolStart` events in `xProcessJsonChunk`.
5. **Verify passing**: All decoder tests pass.

---

### Phase 4: New Component — `MessageStore`

**Spec proposal**: `src/tui/message_store.spec.revise.md`
**New files**: `src/tui/message_store.h`, `src/tui/message_store.cpp`
**Tests**: `test/unit/test_message_store.cpp`

**Changes**:
- Create the `MessageStore` class with `apply()` methods for every event type
- Identity maps: `stream_to_asst_`, `invocation_to_tool_`
- Projected data: `turns_`, `assistants_`, `tools_`
- Dirty tracking: `dirty_ids_`

**TDD steps**:

1. **Stubs**: Create header with full class declaration, empty method bodies returning default values.
2. **Tests**: Write comprehensive tests:
   - **Empty store**: `turn_order()` empty, `consume_dirty()` empty
   - **LlmStart → LlmChunk**: `assistant("asst_1").text == "hello"`, dirty contains `"asst_1"`
   - **LlmStart → ToolStart → ToolChunk → ToolEnd**: `tool_for_invocation(42) == "tc_1"`, `tool("tc_1").output == "result"`, failed
   - **ToolStart with parentStreamId**: `assistant_for_stream(1)` returns correct assistant, `turn(turn_id).tool_ids` contains the tool
   - **Complete with turnSeq**: `turn_order()` contains the turn, dirty set populated
   - **Multiple turns**: Two LlmStart → Complete sequences produce 2 entries in `turn_order()`
   - **consume_dirty()**: Returns changed ids, second call returns empty
   - **clear()**: All data reset
3. **Verify failing**: All tests fail (empty implementations return nothing).
4. **Implement**: Full implementation of every `apply()` method, identity maps, dirty tracking.
5. **Verify passing**: All message store tests pass.

---

### Phase 5: New Component — `StateManager`

**Spec proposal**: `src/tui/state_manager.spec.revise.md`
**New files**: `src/tui/state_manager.h`, `src/tui/state_manager.cpp`
**Tests**: `test/unit/test_state_manager.cpp`

**Changes**:
- Create the `StateManager` class with two-tier resolution

**TDD steps**:

1. **Stubs**: Create header with full declaration, empty method bodies returning defaults.
2. **Tests**:
   - **Global default**: `is_collapsed("any")` returns `true` initially
   - **Per-item override**: `toggle_collapsed("x")` → `is_collapsed("x")` returns `false`
   - **Isolation**: `toggle_collapsed("x")` → `is_collapsed("y")` still returns global default
   - **collapse_all**: After `toggle_collapsed("x")`, calling `collapse_all()` → `is_collapsed("x")` returns `true`
   - **expand_all**: `expand_all()` → `is_collapsed("x")` returns `false`
   - **Toggle back**: `toggle_collapsed("x")` twice → returns global default
3. **Verify failing**: Tests fail (stubs return defaults unconditionally).
4. **Implement**: Full two-tier resolution with overrides map.
5. **Verify passing**: All state manager tests pass.

---

### Phase 6: New Component — `ToolCallComponent`

**Spec proposal**: `src/tui/tool_call_component.spec.revise.md`
**New files**: `src/tui/tool_call_component.h`, `src/tui/tool_call_component.cpp`
**Tests**: `test/unit/test_tool_call_component.cpp`

**Changes**:
- Create retained ComponentBase subclass with stable `Box box_` member
- Reads from `MessageStore` and `StateManager`
- Handles click-to-collapse via `box_.Contain()` + `StateManager::toggle_collapsed()`
- Lazy data loading on expand (framework for future)

**TDD steps**:

1. **Stubs**: Create header and source with `ComponentBase` subclass, empty `OnRender()` returning placeholder, empty `OnEvent()` returning `false`.
2. **Tests**: Write FTXUI-compatible tests or unit tests with a mock MessageStore/StateManager:
   - **Construction**: `tool_id()` returns the ID passed at construction
   - **OnRender non-null**: `Render()` returns a non-null Element
   - **OnEvent click handler**: Simulate mouse event within box → `StateManager::toggle_collapsed()` called
   - **OnEvent miss**: Simulate mouse event outside box → no toggle
   - **Box stability**: `box_` address does not change across multiple `Render()` calls (reference stability test)
   - **Data from Store**: `Render()` produces different output for completed vs running tool states
3. **Verify failing**: Tests fail (empty stubs don't handle events or read Store).
4. **Implement**: Full component with `reflect(box_)`, mouse event handling via `box_.Contain()`, Store reads.
5. **Verify passing**: All tool call component tests pass.

---

### Phase 7: New Component — `EntryComponent` (TurnComponent + AssistantComponent)

**Spec proposal**: `src/tui/entry_component.spec.revise.md`
**New files**: `src/tui/entry_component.h`, `src/tui/entry_component.cpp`
**Tests**: `test/unit/test_entry_component.cpp`

**Changes**:
- `TurnComponent`: Renders one conversation turn. Contains an `AssistantComponent`. Wraps in a container with separation.
- `AssistantComponent`: Renders role label + text + tool children. Has stable `Box` for the header. Reconciles tool children when Store updates.

**TDD steps**:

1. **Stubs**: Create both component classes with minimal `OnRender` returning placeholder text.
2. **Tests**:
   - **TurnComponent construction**: `Render()` returns non-null, contains role label text
   - **AssistantComponent with text**: `Render()` includes the assistant's text from Store
   - **AssistantComponent with tool children**: `Render()` includes tool block headers for each tool in `tool_ids`
   - **Reconcile adds child**: After `reconcile()`, a new tool in Store's `tool_ids` produces an additional tool component
   - **Reconcile removes child**: After `reconcile()`, a removed tool produces one fewer tool component
   - **Header Box stability**: `Box` address stable across `Render()` calls for the assistant header
3. **Verify failing**: Tests fail (empty components don't read Store or create children).
4. **Implement**: Full components with Store reads, child reconciliation, role labels.
5. **Verify passing**: All entry component tests pass.

---

### Phase 8: Shim — Wire Store into AgentTui

**Spec**: `src/tui/agent_tui.spec.md`
**Revision**: `src/tui/agent_tui.spec.revise.md` (Rev 4.1, 4.2)
**Spec**: `src/tui/message_panel.spec.md` (before retained tree conversion)

**Changes**:
- Add `MessageStore m_store` and `StateManager m_stateMgr` to `AgentTui`
- Call `m_store.apply(event)` as the first line of every `xOn*` event handler
- Existing rendering code continues to work unchanged
- TUI E2E tests continue to pass

**TDD steps**:

1. **Stubs**: Add members, add `apply()` calls at the start of each handler.
2. **Tests**: No new unit tests for this phase — the existing E2E test suite (`test_tui_e2e.py`) validates that the store doesn't break existing rendering. But add:
   - Unit test that drains a sequence of events into `m_store` and verifies Store state after processing
3. **Verify failing**: The E2E tests should still pass (no rendering change). The new unit test verifies Store state matches expected projections.
4. **Implement**: Wire the store. The store is write-only at this point — no reads.
5. **Verify passing**: All TUI E2E tests pass, Store projection test passes.

---

### Phase 9: Replace MessagePanel with Retained Tree

**Spec**: `src/tui/message_panel.spec.md`
**Revision**: `src/tui/message_panel.spec.revise.md` (Rev 5.1-5.6)

**Changes**:
- Remove `entryBoxes`, `toolHits` vectors and `ToolHit` struct
- Replace `Renderer` lambda with `Container::Vertical` + `CatchEvent`
- Replace `xRenderEntry` / `xRenderAssistant` / `xRenderToolBlock` with component factories
- Add `xSyncComponents()` to reconcile the component tree with Store
- Remove old imperative public API (`append`, `beginAssistant`, etc.)
- Implement `component()`, `setStore()`, `setStateManager()`, `sync()`, scroll methods

**TDD steps**:

1. **Stubs**: Create new MessagePanel implementation using TurnComponent/ToolCallComponent. Keep old methods as no-ops or deprecated wrappers.
2. **Tests**: Write tests for the new API:
   - **Empty panel**: `component()` returns non-null, `count() == 0`
   - **Sync with one turn**: After `setStore()` + `sync()`, component tree has 1 TurnComponent child
   - **Sync with multiple turns**: Component tree matches Store's `turn_order()`
   - **Second sync adds new turns**: After store update + sync, new components appear
   - **Scroll methods**: `scrollUp`/`scrollDown`/`scrollToBottom`/`isAtBottom` work correctly
   - **No ToolHit vectors**: Verify `entryBoxes` and `toolHits` no longer exist in Impl
3. **Verify failing**: E2E tests will fail because AgentTui still calls old API methods. The new unit tests also fail (empty stubs).
4. **Implement**: Full retained tree MessagePanel. Keep old API methods as thin wrappers that call the new store-based implementation (or remove them entirely and update AgentTui).
5. **Verify passing**: All unit tests pass. E2E tests pass (old AgentTui calls updated to use sync pattern).

---

### Phase 10: Wire AgentTui to Component Tree (Remove Old API Calls)

**Spec**: `src/tui/agent_tui.spec.md`
**Revision**: `src/tui/agent_tui.spec.revise.md` (Rev 4.3, 4.4)

**Changes**:
- Remove `m_streamingText`, `m_assistantEntryIndex`, `m_hasActiveStream` from AgentTui
- Remove direct calls to old MessagePanel API (`appendOrUpdateAssistantText`, `appendAssistantTool`, etc.)
- After event handlers call `m_store.apply(event)`, call `m_messagePanel->sync()` to update the component tree
- Simplify `xOn*` handlers to struct-based signatures

**TDD steps**:

1. **Stubs**: Remove old MessagePanel API calls. Replace with `m_store.apply(ev)` + `m_messagePanel->sync()`.
2. **Tests**: E2E tests are the primary validation. Additionally:
   - Unit tests for Store state after a full event sequence (LlmStart → Chunk → ToolStart → Chunk → End → Complete)
   - Verify `m_messagePanel->count()` returns correct entry count after sync
3. **Verify failing**: E2E tests may fail if `sync()` doesn't produce the correct component tree.
4. **Implement**: Wire `sync()` calls after each event. Remove old streaming cursor members.
5. **Verify passing**: All TUI E2E tests pass. AgentTui no longer has `m_assistantEntryIndex` or `m_streamingText`.

---

### Phase 11: Clean Up Deprecated Code

**Changes**:
- Remove `ToolHit` struct from `message_panel.h`
- Remove `entryBoxes`, `toolHits` from `Impl`
- Remove old render methods (`xRenderEntry`, `xRenderAssistant`, `xRenderToolBlock`)
- Verify no remaining cursor state exists

**Tests**: Full E2E suite + unit tests. All must pass.

---

## Test Infrastructure Notes

### Existing E2E Tests
Location: `test/e2e/test_tui_e2e.py`
Runner: `bash test/e2e/test_tui_e2e.sh`

Key test classes:
- `TestTuiBasic` — startup, goal submission, commands
- `TestTuiMultiTurn` — multi-turn context preservation (critical for retained tree)
- `TestTuiScrolling` — scroll behavior (critical after scroll management migration)
- `TestTuiToolDisplay` — tool block rendering (critical for ToolCallComponent)
- `TestCliCrash` — crash survival (important after architecture changes)

### New Unit Tests
Create new test files:
- `test/unit/test_message_store.cpp`
- `test/unit/test_state_manager.cpp`
- `test/unit/test_tool_call_component.cpp`
- `test/unit/test_entry_component.cpp`

Add to CMakeLists.txt under the appropriate test target or create a new test target.

### Running Tests
```bash
# Unit tests
cmake --build build && cd build && ctest -R "message_store|state_manager|tool_call|entry_component" -V

# TUI E2E tests
bash test/e2e/test_tui_e2e.sh

# Full test suite
bash test/e2e/run_all_tests.sh
```

### TRACE_LOG Instrumentation
Where useful, add `TRACE_LOG` statements to verify:
- Store `apply()` invocation counts
- Component tree child count changes after sync
- Dirty set size after batch event processing

Build with: `cmake -B build -DENABLE_TRACE=ON`

---

## Rollback Strategy

Each phase is reversible:
- Phase 1-3: Revert struct field additions, rebuild
- Phase 4-7: Delete new files, revert CMakeLists.txt
- Phase 8-10: Restore old AgentTui event handlers, revert MessagePanel

If an E2E test fails after any phase:
1. Check if the test fixture needs updating (the mock server or scenario)
2. Check if the spec contradicts the test behavior
3. If spec is correct, fix the implementation
4. If spec is wrong, notify the user before modifying the spec

## File Manifest

### Modified
| File | Phase |
|------|-------|
| `src/shared/mpsc.h` | 1 |
| `src/core/driven_core.h` | 2 |
| `src/core/driven_core.cpp` | 2 |
| `src/llm/response_decoder.cpp` | 3 |
| `src/tui/agent_tui.h` | 8, 10 |
| `src/tui/agent_tui.cpp` | 8, 10 |
| `src/tui/message_panel.h` | 9, 11 |
| `src/tui/message_panel.cpp` | 9, 11 |

### New
| File | Phase |
|------|-------|
| `src/tui/message_store.h` | 4 |
| `src/tui/message_store.cpp` | 4 |
| `src/tui/state_manager.h` | 5 |
| `src/tui/state_manager.cpp` | 5 |
| `src/tui/tool_call_component.h` | 6 |
| `src/tui/tool_call_component.cpp` | 6 |
| `src/tui/entry_component.h` | 7 |
| `src/tui/entry_component.cpp` | 7 |
| `test/unit/test_message_store.cpp` | 4 |
| `test/unit/test_state_manager.cpp` | 5 |
| `test/unit/test_tool_call_component.cpp` | 6 |
| `test/unit/test_entry_component.cpp` | 7 |
