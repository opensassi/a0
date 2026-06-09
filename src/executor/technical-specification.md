# Technical Specification: Executor Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

The Executor sub-module owns all tool execution infrastructure — subprocess management, tool running, state management, dependency batching, and system tool handlers. It is formed by merging the old `a0_lib` executor files with the former `cmd_runner_lib`.

**Source files:**
- `command_runner.h/.cpp` — subprocess fork/exec/alarm management
- `tool_runner.h/.cpp` — HostToolRunner (stdin/args mode, timeout, streaming)
- `tool_state.h/.cpp` — per-session key-value state bag
- `dependency_graph.h/.cpp` — reader/writer classification + batch execution
- `system_handlers.h/.cpp` — C++ handler functions (xBash, xRead, xEdit, xWrite, xGlob, xGrep, etc.)
- `docker_security_filter.h/.cpp` — Docker command security filtering
- `stream_registry.h/.cpp` — streaming IPC support

**Dependencies:** `shared_lib`

**Namespace:** `a0`, `a0::skills` (for HandlerContext referenced by system_handlers)

---

## 2. Component Specifications

All class interfaces are unchanged from the existing specification. Key classes:

```cpp
namespace a0 {

class CommandRunner {
    // Subprocess management: run(), runStreaming(), runAll()
};

class ToolRunner {
    // run(tool, params) → json, runStreaming()
};

class HostToolRunner : public ToolRunner { };

class ToolState {
    // Thread-safe per-session key-value state: set(), get(), has(), remove(), clear()
};

class DependencyGraph {
    // classifyTool(), buildBatches(), executeBatches()
};

class StreamRegistry {
    // Stream handle management
};

} // namespace a0

namespace a0::skills {
    // system_handlers: xBash, xRead, xEdit, xWrite, xGlob, xGrep, etc.
    struct HandlerContext;
    struct HandlerResult;
}
```

---

## 3. Build System

```cmake
add_library(executor_lib STATIC
    command_runner.cpp
    tool_runner.cpp
    tool_state.cpp
    dependency_graph.cpp
    system_handlers.cpp
    docker_security_filter.cpp
    stream_registry.cpp
)
target_include_directories(executor_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(executor_lib PUBLIC shared_lib)
```

---

## 4. Testing

| Test File | Action |
|-----------|--------|
| `test_tool_runner.cpp` | Update `#include "tool_runner.h"` → `#include "executor/tool_runner.h"`; link `executor_lib` |
| `test_dependency_graph.cpp` | Update `#include "dependency_graph.h"` → `#include "executor/dependency_graph.h"` |
| `test_tool_state.cpp` | Update `#include "tool_state.h"` → `#include "executor/tool_state.h"` |
| `test_system_tools.cpp` | Update `#include "system_handlers.h"` → `#include "executor/system_handlers.h"` |
| `test_pipeline_execution.cpp` | Update includes + link `executor_lib` |
