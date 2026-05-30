# ReplayEngine Spec

## 1. Overview

Reads a stored session from `PersistenceStore` and replays it against the current binary. LLM responses are injected from the log. Tools are re-executed via `CommandRunner` and their outputs compared against stored results. Used for upgrade validation to detect behavioral regressions.

**Source files:** `src/persistence/replay_engine.h/.cpp`

**Dependencies:** `PersistenceStore`, `CommandRunner`

## 2. Component Specifications

```cpp
class ReplayEngine {
public:
    explicit ReplayEngine(PersistenceStore* store);

    int replay(int64_t sessionId, std::string& divergence);
    int replayTo(int64_t sessionId, int64_t upToMessageId, std::string& divergence);
};
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| replay matching session | Returns 0, divergence empty |
| replay with tool output mismatch | Returns 1, divergence contains details |
| replay unknown session | Returns -1 |
