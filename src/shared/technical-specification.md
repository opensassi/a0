# Technical Specification: Shared Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

The Shared sub-module collects all **header-only** files that are consumed by every other sub-module. It has no `.cpp` source files and produces no linkable artifact — it is a CMake INTERFACE target that propagates include directories and usage requirements.

**Source files:** (all header-only)
- `agent_interfaces.h` — core data structures (Tool, ToolCall, ToolSchema, Message, TrustLevel, etc.)
- `mpsc.h` — MPSC channel types (Command, AppCoreEvent, channel templates)
- `trace.h` — TRACE_LOG macro for debug logging
- `hex_session_id.h` — UUID generation utility
- `daemonize.h` — daemonization helpers
- `handler_results.h` — HandlerResult struct for system tool dispatch
- `resource_provider.h` — abstract ResourceProvider interface (placeholder until implementation in next session)

**Dependencies:** None (standard library only + nlohmann/json for agent_interfaces.h)

**Namespace:** `a0` (shared namespace — no sub-namespace)

---

## 2. Component Specifications

```cpp
// agent_interfaces.h — unchanged from existing; moved here from src/
// mpsc.h — unchanged from existing; moved here from src/
// trace.h — unchanged from existing; moved here from src/
// hex_session_id.h — unchanged from existing; moved here from src/
// daemonize.h — unchanged from existing; moved here from src/
// handler_results.h — unchanged from existing; moved here from src/
```

### 2.1 ResourceProvider (new, interface only)

```cpp
namespace a0 {

using StreamId = int64_t;
using InvocationId = int64_t;

enum class ResourceType { LlmStream, ToolOutput, TerminalStream, ToolInvocation };

class ResourceHandle {
public:
    virtual ~ResourceHandle() = default;
    virtual int64_t id() const = 0;
    virtual bool hasMore() const = 0;
    virtual std::string readNext() = 0;
    virtual std::string read(int64_t offset, int64_t limit) = 0;
    virtual int64_t size() const = 0;
};

class ResourceWriter {
public:
    virtual ~ResourceWriter() = default;
    virtual int64_t id() const = 0;
    virtual void append(const std::string& data) = 0;
    virtual void close() = 0;
    virtual bool closed() const = 0;
};

class ResourceProvider {
public:
    virtual ~ResourceProvider() = default;
    virtual std::unique_ptr<ResourceWriter> create(ResourceType type) = 0;
    virtual std::unique_ptr<ResourceHandle> open(ResourceType type, int64_t id) = 0;
};

} // namespace a0
```

---

## 3. Build System

```cmake
add_library(shared_lib INTERFACE)
target_include_directories(shared_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shared_lib INTERFACE nlohmann_json::nlohmann_json)
```

No `.cpp` sources. All consumers link via `target_link_libraries(my_lib PUBLIC shared_lib)`.

---

## 4. Testing

Testing of individual headers is done as part of the consuming sub-module's test suite. No dedicated test target for `shared_lib`.
