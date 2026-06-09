# Technical Specification: IPC Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

The IPC sub-module owns all inter-process communication infrastructure — Unix domain sockets, JSON-line message framing, and the Cap'n Proto schema for future IPC protocol. It is promoted from flat `src/` files into its own sub-module.

**Source files:**
- `unix_socket.h/.cpp` — Unix domain socket wrapper (create, bind, listen, accept, connect, send, recv)
- `ipc_protocol.h/.cpp` — JSON-line message framing (Message type, serialize/deserialize, BufferedSocket)
- `app_core_event.capnp` — Cap'n Proto schema (placeholder, implementation deferred)

**Dependencies:** `shared_lib`, `capnp::capnp` (future)

**Namespace:** `a0::ipc`

---

## 2. Component Specifications

```cpp
namespace a0::ipc {

class UnixSocket {
    // create(), bind(), listen(), accept(), connect()
    // send(), recv(), poll(), close()
};

struct Message {
    std::string type;
    nlohmann::json data;
    // ...payload fields...
};

class BufferedSocket {
    // Buffered reader for framed JSON-line protocol
    // recvMessage(), sendMessage()
};

} // namespace a0::ipc
```

All interfaces are unchanged from the existing specification.

---

## 3. Build System

```cmake
# Cap'n Proto code generation (future)
# capnp_generate_cpp(app_core_event_capnp app_core_event.capnp)

add_library(ipc_lib STATIC
    unix_socket.cpp
    ipc_protocol.cpp
    # ${app_core_event_capnp}  (future)
)
target_include_directories(ipc_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # ${CMAKE_CURRENT_BINARY_DIR}  (for generated capnp headers, future)
)
target_link_libraries(ipc_lib PUBLIC
    shared_lib
    # capnp::capnp  (future)
)
```

---

## 4. Testing

| Test | Action |
|------|--------|
| `test_unix_socket.cpp` | Update `#include "unix_socket.h"` → `#include "ipc/unix_socket.h"`; link `ipc_lib` |
| `test_ipc_protocol.cpp` | Update `#include "ipc_protocol.h"` → `#include "ipc/ipc_protocol.h"` |
| `test_buffered_socket.cpp` | Update `#include "ipc_protocol.h"` → `#include "ipc/ipc_protocol.h"` |
