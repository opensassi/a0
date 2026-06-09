# Sub-Module Reorganization Plan

Incorporates all decisions from the persistence-first I/O plan
(`NEW-IO-IMPLEMENTATION-PLAN.md`).

---

## Current State

`a0_lib` is a monolithic library of 23 source files in `src/` with no internal structure. Six sub-modules exist (`persistence/`, `skills/`, `docker/`, `b1/`, `c2/`, `tui/`) but three of them include headers from the monolithic `src/` via relative paths, creating reverse dependencies.

**Dead code still compiled:**
- `src/context_manager.*` — compiled, unreferenced
- `src/dependency_resolver.*` — compiled, unreferenced
- `src/dependency_resolver.*` — compiled, unreferenced

**Dead code not compiled (present but inert):**
- `src/agent_core.*`
- `src/skill_runner.*`

**`ipc_lib` and `cmd_runner_lib`** already exist as separate libraries but their source files sit at `src/` level rather than in a directory.

---

## Target Directory Structure

```
src/
├── shared/                          ← header-only, zero dependencies
│   ├── agent_interfaces.h           ← trimmed: Tool, ToolCall, ToolSchema, Message, TrustLevel
│   ├── mpsc.h
│   ├── trace.h
│   ├── hex_session_id.h
│   ├── daemonize.h
│   └── resource_provider.h          ← NEW: abstract persistence interface
│
├── ipc/                             ← promoted from loose src/ files
│   ├── CMakeLists.txt
│   ├── app_core_event.capnp         ← NEW: Cap'n Proto schema
│   ├── unix_socket.h/.cpp
│   └── ipc_protocol.h/.cpp
│
├── llm/                             ← NEW: extracted from src/
│   ├── CMakeLists.txt
│   ├── llm_provider.h               ← abstract async LLM interface
│   ├── driven_provider.h/.cpp       ← curl_multi base
│   ├── deepseek_provider.h/.cpp     ← DeepSeek subclass
│   └── response_decoder.h/.cpp      ← SSE/JSON decoder
│
├── executor/                        ← NEW: extracted from src/
│   ├── CMakeLists.txt
│   ├── command_runner.h/.cpp        ← subprocess management
│   ├── tool_runner.h/.cpp           ← HostToolRunner
│   ├── tool_state.h/.cpp            ← per-session state bag
│   ├── dependency_graph.h/.cpp      ← reader/writer batching
│   ├── system_handlers.h/.cpp       ← C++ handler functions
│   ├── docker_security_filter.h/.cpp
│   ├── stream_registry.h/.cpp
│   └── handler_results.h
│
├── core/                            ← NEW: the CONTROLLER
│   ├── CMakeLists.txt
│   ├── driven_core.h/.cpp           ← tick-based state machine
│   └── app_core_thread.h/.cpp       ← thread wrapper + MPSC dispatch
│
├── bootstrap/                       ← NEW: startup & config
│   ├── CMakeLists.txt
│   ├── a0_dir.h/.cpp                ← .a0/ directory lifecycle
│   ├── base_prompt.h/.cpp           ← system prompt construction
│   ├── personas.h/.cpp              ← persona system
│   └── session_context.h/.cpp       ← git context, worktree
│
├── persistence/                     ← existing
│   ├── CMakeLists.txt
│   ├── persistence_store.h          ← abstract interface (kept for tests)
│   ├── sqlite_store.h/.cpp
│   ├── sqlite_resource_provider.h/.cpp  ← NEW: ResourceProvider impl
│   ├── replay_engine.h/.cpp         ← simplified to read-only verifier
│   └── build_identity.h/.cpp
│
├── skills/                          ← existing (unchanged)
│   ├── CMakeLists.txt
│   ├── skills.h, skill_manager.*, skill_loader.*
│   ├── version_manager.*, validation_engine.*
│   └── valijson dependency
│
├── docker/                          ← existing (unchanged)
│   ├── CMakeLists.txt
│   ├── container_manager.*, compose_manager.*
│   ├── docker_tool_runner.*, dependency_installer.*
│   └── docker_cli_wrapper.*
│
├── tui/                             ← existing (unchanged)
│   ├── CMakeLists.txt
│   ├── agent_tui.*, message_panel.*, input_panel.*
│   ├── status_bar.*, dialog_manager.*
│   ├── markdown_renderer.*, clipboard.*, styles.*
│   └── ftxui + md4c dependencies
│
├── b1/                              ← existing (separate executable)
│   ├── CMakeLists.txt
│   ├── supervisor.*, a0_launcher.*
│   └── b1_main.cpp
│
├── c2/                              ← existing (separate executable)
│   ├── CMakeLists.txt
│   ├── b1_registry.*, c2_listener.*
│   ├── dashboard_server.*, sse_manager.*
│   └── c2_event_store.*  ← REMOVED (phase 3 of IO plan)
│
├── main.cpp                         ← stays (entry point + DI wiring)
│
└── (deleted)
    ├── agent_core.cpp/.h
    ├── agent_core.spec.md
    ├── skill_runner.cpp/.h
    ├── skill_runner.spec.md
    ├── context_manager.cpp/.h
    ├── context_manager.spec.md
    ├── dependency_resolver.cpp/.h
    └── dependency_resolver.spec.md
```

---

## Build Dependency Graph

```
a0 (executable)
└── main.cpp
    ├── core_lib (CONTROLLER)
    │   ├── llm_lib
    │   │   ├── shared/          (agent_interfaces.h, mpsc.h, resource_provider.h)
    │   │   ├── libcurl
    │   │   └── nlohmann_json
    │   ├── executor_lib
    │   │   └── shared/
    │   ├── skills_lib
    │   │   ├── shared/
    │   │   └── valijson
    │   ├── persistence_lib
    │   │   ├── shared/
    │   │   └── SQLite3
    │   ├── docker_lib
    │   │   └── shared/
    │   ├── bootstrap_lib
    │   │   └── shared/
    │   ├── ipc_lib
    │   │   ├── shared/
    │   │   └── capnproto
    │   └── tui_lib
    │       ├── shared/
    │       ├── ftxui
    │       └── md4c
    └── a0_lib (phased out — rename to shared_lib or remove)

b1 (executable)
└── b1_lib
    ├── ipc_lib          ← Cap'n Proto message forwarding
    ├── executor_lib     ← a0_launcher (fork/exec)
    └── shared/

c2 (executable)
└── c2_lib
    ├── ipc_lib          ← Cap'n Proto event stream
    ├── uSockets         ← HTTP/SSE server
    ├── OpenSSL
    ├── SQLite3          ← read-only resource proxy
    └── shared/
```

### Key changes from current build:

| Change | Reason |
|---|---|
| **`core_lib`** owns all coordination; it links every sub-module | Single controller, clear dependency direction |
| **`persistence_lib`** no longer links `cmd_runner_lib` | ReplayEngine is read-only; tools are never re-executed |
| **`executor_lib`** owns `command_runner.*` | All tool execution in one place |
| **`skills_lib`** depends only on `shared/` | No more reverse include into `a0_lib`; uses `ResourceProvider*` for I/O |
| **`shared/`** is header-only | No link step needed; all consumers include directly |
| **`a0_lib`** is eliminated | Replaced by individual library targets |

---

## Library Definitions

### shared_lib (header-only, no .cpp)

```
target_include_directories(shared_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
```

No `add_library()` with sources — just an INTERFACE target for include propagation.

### llm_lib

```cmake
add_library(llm_lib STATIC
    driven_provider.cpp
    deepseek_provider.cpp
    response_decoder.cpp
)
target_include_directories(llm_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llm_lib PUBLIC
    shared_lib
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
```

### executor_lib

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

### persistence_lib (updated)

```cmake
add_library(persistence_lib STATIC
    sqlite_store.cpp
    sqlite_resource_provider.cpp    # NEW
    replay_engine.cpp               # simplified, no cmd_runner
    build_identity.cpp
)
target_include_directories(persistence_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(persistence_lib PUBLIC
    shared_lib
    SQLite::SQLite3
)
```

### core_lib (controller)

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

### ipc_lib (updated)

```cmake
# Cap'n Proto code generation
capnp_generate_cpp(app_core_event_capnp app_core_event.capnp)

add_library(ipc_lib STATIC
    unix_socket.cpp
    ipc_protocol.cpp
    ${app_core_event_capnp}
)
target_include_directories(ipc_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}   # for generated capnp headers
)
target_link_libraries(ipc_lib PUBLIC
    shared_lib
    capnp::capnp
)
```

### a0 executable

```cmake
add_executable(a0 src/main.cpp)
target_link_libraries(a0 PRIVATE core_lib)
```

Note: direct linking `core_lib` transitively brings in everything via `PUBLIC` link deps.

### b1 executable (updated)

```cmake
# b1_lib unchanged in sources, simplified in routing logic
target_link_libraries(b1_lib PUBLIC
    ipc_lib           # Cap'n Proto event codec
    executor_lib      # a0_launcher (fork/exec)
    shared_lib
)
```

### c2 executable (updated)

```cmake
# c2_lib — remove c2_event_store.*
target_link_libraries(c2_lib PUBLIC
    ipc_lib           # Cap'n Proto event codec
    SQLite::SQLite3   # read-only resource proxy
    uSockets
    shared_lib
)
```

---

## Migration Order

| Step | Action |
|---|---|
| **1** | Create `src/shared/` directory, move header-only files, create INTERFACE target |
| **2** | Create `src/llm/`, `src/executor/`, `src/core/`, `src/bootstrap/`, `src/ipc/` directories with CMakeLists.txt |
| **3** | Move source files into new directories (no code changes — pure file moves + include path updates) |
| **4** | Delete dead code files (`agent_core.*`, `skill_runner.*`, `context_manager.*`, `dependency_resolver.*`) |
| **5** | Remove `a0_lib` from root CMakeLists.txt; link `core_lib` instead |
| **6** | Remove `cmd_runner_lib` dependency from `persistence_lib`; simplify `ReplayEngine` |
| **7** | Update `b1_lib` and `c2_lib` link dependencies |
| **8** | Update `skills_lib` includes to use `shared/` instead of `../` relative paths |
| **9** | Run full test suite: `cmake --build build && ctest --output-on-failure` |
