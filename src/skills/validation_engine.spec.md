# ValidationEngine Spec

## 1. Overview

Replays historical tool invocations against a candidate version of a skill component. Uses `CommandRunner` to re-execute tools and compares outputs against stored results. Reports divergences for upgrade validation.

**Source files:** `src/skills/validation_engine.h/.cpp`

**Dependencies:** `CommandRunner`, `PersistenceStore` (SQLite)

## 2. Component Specifications

```cpp
namespace a0::persistence { class PersistenceStore; }

class ValidationEngine {
public:
    /// \param store  Persistence store for invocation history (SQLite).
    explicit ValidationEngine(a0::persistence::PersistenceStore* store);

    int validate(SkillNamespace ns, const std::string& component,
                 const SkillManifest& manifest, const std::string& commit,
                 std::string& report);
};
```

## 3. Architecture

Historical invocation records are now read from the persistence store (SQLite `invocation` table) instead of filesystem JSONL files. The `xLoadLogs` helper queries `m_store->loadInvocations(type, component)` and parses the results into `InvocationRecord` structs.

## 4. Testing Requirements

| Test | Verification |
|------|-------------|
| validate with matching output | Returns 0, report empty |
| validate with divergence | Returns non-zero, details in report |
| validate with unknown component | Returns -1 |
| validate with persistence store | Reads from SQLite, no filesystem dependency |
