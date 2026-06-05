# ValidationEngine Spec

## 1. Overview

Replays historical tool invocations against a candidate version of a skill component. Uses `CommandRunner` to re-execute tools and compares outputs against stored results. Reports divergences for upgrade validation. Also supports compatibility bridges: when a direct output comparison fails, `ValidationEngine` tries each `CompatBridge` (from the manifest) to transform the historical output before re-comparing.

**Source files:** `src/skills/validation_engine.h/.cpp`

**Dependencies:** `CommandRunner`, `PersistenceStore` (SQLite)

## 2. Component Specifications

```cpp
namespace a0::persistence { class PersistenceStore; }

namespace a0::skills {

class ValidationEngine {
public:
    /// \param store  Persistence store for invocation history (SQLite).
    explicit ValidationEngine(::a0::persistence::PersistenceStore* store);

    /// \param ns        Skill namespace (SYSTEM/LOCAL/GITHUB).
    /// \param component Component name.
    /// \param manifest  Candidate manifest to validate against.
    /// \param commit    Commit identifier (for logging).
    /// \param report    Output report string.
    /// \retval 0  All invocations matched, no bridges used.
    /// \retval 1  All invocations matched after applying compatibility bridges.
    /// \retval -1 One or more invocations diverged (details in report).
    int validate(SkillNamespace ns,
                 const std::string& component,
                 const SkillManifest& manifest,
                 const std::string& commit,
                 std::string& report);

private:
    ::a0::persistence::PersistenceStore* m_store;

    /// Re-execute a historical invocation via CommandRunner.
    int xReplay(const InvocationRecord& record,
                const SkillManifest& manifest,
                const std::string& toolName,
                nlohmann::json& actualOutput);

    /// Compare expected vs actual output.
    int xCompare(const nlohmann::json& expected,
                 const nlohmann::json& actual,
                 const ToolSchema& schema);

    /// Run a compatibility bridge command to transform output format.
    int xApplyBridge(const CompatBridge& bridge,
                     const nlohmann::json& input,
                     nlohmann::json& output);

    /// Load invocation logs from persistence store.
    std::vector<InvocationRecord> xLoadLogs(const std::string& ns,
                                             const std::string& component) const;
};

} // namespace a0::skills
```

## 3. Architecture

Historical invocation records are read from the persistence store (SQLite `invocation` table) instead of filesystem JSONL files. The `xLoadLogs` helper queries `m_store->loadInvocations(type, component)` and parses the results into `InvocationRecord` structs.

```
xLoadLogs → m_store->loadInvocations(type, component)
                ↓
          InvocationRecord { toolName, params, output, timestamp }
                ↓
          For each record:
              xReplay → CommandRunner::run → actualOutput
                  ↓
              xCompare(expected, actual, schema)
                  ↓
              If mismatch:
                  For each CompatBridge in manifest:
                      xApplyBridge(bridge, params, bridgedOutput)
                      xCompare(expected, bridgedOutput, schema)
                  If still mismatch: report divergence
```

## 4. Return Values

| Return | Meaning |
|--------|---------|
| 0 | All invocations matched directly |
| 1 | All invocations matched via compat bridges |
| -1 | One or more divergences found (in report) |

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| validate with matching output | Returns 0, report empty |
| validate with divergence | Returns -1, details in report |
| validate with compat bridge success | Returns 1, bridgesUsed > 0 |
| validate with unknown component | Returns -1 |
| validate with empty logs | Returns 0, report="no historical logs..." |
| validate with persistence store | Reads from SQLite, no filesystem dependency |
