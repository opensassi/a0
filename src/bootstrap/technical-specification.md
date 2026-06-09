# Technical Specification: Bootstrap Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

The Bootstrap sub-module owns all startup and configuration logic — the `.a0/` directory lifecycle, persona system, base prompt construction, and session context (git info, worktree). These are the first components initialized during agent startup.

**Source files:**
- `a0_dir.h/.cpp` — `.a0/` directory lifecycle (create, gitignore)
- `base_prompt.h/.cpp` — system prompt construction
- `personas.h/.cpp` — persona system (PersonaLoader)
- `session_context.h/.cpp` — session context (git info, worktree, container name)
- `hex_session_id.h` — UUID generation (header-only)
- `daemonize.h` — daemonization helpers (header-only)

**Dependencies:** `shared_lib`

**Namespace:** `a0`

---

## 2. Component Specifications

```cpp
namespace a0 {

class A0DirManager {
    // ensureA0Dir(), ensureGitIgnore()
};

class PersonaLoader {
    // loadAll(), listPersonas(), getPersona()
    // Three-tier: system/, local/, github_<user>/
};

class SessionContext {
    // containerName(), originalCwd(), gitInfo()
    // loadFromDb(), saveToDb(), init()
};

// Free functions:
std::string buildBasePrompt(const std::string& personaName,
                            const std::string& buildHash,
                            const std::string& osInfo,
                            const std::string& cwd);
std::string generateHexSessionId();

// Daemonize helpers in daemonize.h:
void daemonize();
int writePidFile(const std::string& path);

} // namespace a0
```

All public interfaces are unchanged from the existing specification.

---

## 3. Build System

```cmake
add_library(bootstrap_lib STATIC
    a0_dir.cpp
    base_prompt.cpp
    personas.cpp
    session_context.cpp
)
target_include_directories(bootstrap_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bootstrap_lib PUBLIC shared_lib)
```

Header-only files (`hex_session_id.h`, `daemonize.h`) are accessed via `shared_lib` or by the library's own include path.

---

## 4. Testing

| Test | Action |
|------|--------|
| `test_a0_dir.cpp` | Update `#include "a0_dir.h"` → `#include "bootstrap/a0_dir.h"`; link `bootstrap_lib` |
| `test_session_context.cpp` | Update includes + link `bootstrap_lib` |
| `test_personas.cpp` | Update includes + link `bootstrap_lib` |
