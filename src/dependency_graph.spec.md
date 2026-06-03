# DependencyGraph Spec

## 1. Overview

Builds safe execution order from a set of tool invocations by classifying tools as readers, writers, or read-write based on their qualified names. Readers are parallelized within a batch; writers and read-write tools execute one-at-a-time. Integrates with `CommandRunner::runAll` for subprocess-level parallel execution of pure-reader command tools.

**Source files:** `src/dependency_graph.h/.cpp`

**Dependencies:** `skills/skills.h` (SkillManager, SkillTool), `command_runner.h`

## 2. Component Specifications

```cpp
namespace a0 {

using json = nlohmann::json;

enum class ResourceClass {
    READER,      ///< Pure filesystem reads — safe to run in parallel
    WRITER,      ///< Pure filesystem writes — must not overlap
    READ_WRITE   ///< Opaque/unknown access — run after all readers
};

struct ToolInvocation {
    std::string qualifiedName;
    json params;
    int* seq = nullptr;             ///< persistence counter (nullptr = skip)
    std::string toolCallId;
    int64_t subSessionId = 0;
};

struct BatchResult {
    std::vector<std::string> outputs;   ///< one per invocation, same order as input
    std::vector<std::string> errors;    ///< non-empty for invocations that failed
};

class DependencyGraph {
public:
    /// Classify a tool by its qualified name (system handler path or command tool).
    static ResourceClass classifyTool(const std::string& qualifiedName);

    /// Build execution batches.
    /// Batch 0 = all READERs (safe to parallelize).
    /// Batch 1+ = one WRITER per batch (serialized).
    /// Batch N+ = one READ_WRITE per batch (serialized).
    static std::vector<std::vector<ToolInvocation>> buildBatches(
        const std::vector<ToolInvocation>& invocations);

    /// Execute batches through SkillManager.
    /// READER batch tools are dispatched via executeToolWithMeta in list order.
    /// When all tools in a READER batch are non-systemTool command tools,
    /// delegates to CommandRunner::runAll for true subprocess-level parallelism.
    /// WRITER and READ_WRITE batches execute one tool at a time.
    static std::vector<BatchResult> executeBatches(
        const std::vector<std::vector<ToolInvocation>>& batches,
        a0::skills::SkillManager* skillMgr,
        int maxParallel = 4);

private:
    static json xExecuteOne(const ToolInvocation& inv,
                             a0::skills::SkillManager* skillMgr);
};

} // namespace a0
```

## 3. Classification Table

Built-in classification uses prefix matching against qualified names:

| Class | Prefixes |
|-------|----------|
| READER | `system-fs-read`, `system-fs-glob`, `system-fs-grep`, `system-meta-` |
| WRITER | `system-fs-write`, `system-fs-edit` |
| READ_WRITE | Everything else (bash, git, docker, command tools) |

## 4. Batch Execution

```mermaid
sequenceDiagram
    participant Loop as xRunForkedLoop
    participant DG as DependencyGraph
    participant SM as SkillManager
    participant CR as CommandRunner

    Loop->>DG: buildBatches(invocations)
    DG->>DG: classify each → readers/writers/readWrite
    DG-->>Loop: batches

    Loop->>DG: executeBatches(batches, skillMgr, maxParallel)

    rect rgb(230, 255, 230)
        Note over DG,CR: Batch 0: all READERs
        alt all are command tools
            DG->>CR: runAll(commands, timeout, maxParallel)
            CR-->>DG: [outputs]
        else mixed system + command
            loop each reader
                DG->>SM: executeToolWithMeta(qn, params)
                SM-->>DG: HandlerResult
            end
        end
    end

    rect rgb(255, 240, 230)
        Note over DG,SM: Batch 1+: one WRITER per batch
        DG->>SM: executeToolWithMeta(qn, params)
        SM-->>DG: HandlerResult
    end

    DG-->>Loop: [BatchResult...]
```

## 5. Error Handling

| Condition | Behaviour |
|-----------|-----------|
| Null SkillManager | Returns `json("ERROR: no SkillManager available")` |
| Empty invocations vector | Returns empty batches vector |
| Tool lookup failure | Error propagated from SkillManager |
| Subprocess execution failure | Error output captured in BatchResult.errors |

## 6. Testing Requirements

| Method | Test Case | Expected |
|--------|-----------|----------|
| `classifyTool` | system-fs-read prefix | READER |
| `classifyTool` | system-fs-write prefix | WRITER |
| `classifyTool` | system-bash prefix | READ_WRITE |
| `classifyTool` | local:custom:tool | READ_WRITE |
| `buildBatches` | All readers | Single batch |
| `buildBatches` | One writer | Two batches: readers + writer |
| `buildBatches` | Multiple writers | readers + writer1 + writer2 |
| `buildBatches` | Mixed readers/writers/readWrite | Correct ordering |
| `buildBatches` | Empty input | Empty vector |
| `executeBatches` | Null skillMgr | Error string per invocation |
| `executeBatches` | Reader command tools | runAll called with subprocess commands |
