# Technical Specification: Core Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

The Core sub-module is the **controller** — it owns the tick-driven state machine (`DrivenCore`) and its thread wrapper (`AppCoreThread`). It is the single point of coordination that links all other sub-modules together.

**Source files:**
- `driven_core.h/.cpp` — tick-based state machine
- `app_core_thread.h/.cpp` — thread wrapper + MPSC command dispatch

**Dependencies:** `llm_lib`, `executor_lib`, `skills_lib`, `persistence_lib`, `docker_lib`, `bootstrap_lib`, `ipc_lib`, `tui_lib`

**Namespace:** `a0`

---

## 2. Component Specifications

```cpp
namespace a0 {

class DrivenCore {
    // States: Idle → AwaitingLlm → ExecutingTools → Idle
    // submitGoal(goal), tick(), runSync(goal)
    // Uses LlmProvider*, SkillManager*, PersistenceStore*
    // xBuildToolSchemas(), xHandleLlmEvents(), xExecuteTools()
};

class AppCoreThread {
    // start(cmdRcvr, evtSender, wakeupFn)
    // stop(), running()
    // Owns DrivenCore + DrivenProvider in dedicated thread
    // ppoll()-based event loop
};

} // namespace a0
```

All public interfaces are unchanged from the existing specification. The only change is the location of the files and how they resolve their dependencies.

---

## 3. Build System

```cmake
add_library(core_lib STATIC
    driven_core.cpp
    app_core_thread.cpp
)
target_include_directories(core_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core_lib PUBLIC
    llm_lib
    executor_lib
    skills_lib
    persistence_lib
    docker_lib
    bootstrap_lib
    ipc_lib
    tui_lib
)
```

---

## 4. Testing

| Test | Action |
|------|--------|
| `test_driven_core_persistence.cpp` | Update includes: `#include "driven_core.h"` → `#include "core/driven_core.h"`, `#include "llm_provider.h"` → `#include "llm/llm_provider.h"` |
| `test_tui_integration.cpp` | Updates through transitive deps |
