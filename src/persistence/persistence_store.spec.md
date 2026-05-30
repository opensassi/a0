# PersistenceStore Spec

## 1. Overview

Abstract interface for session persistence. Records every agent I/O (user input, LLM request/response, tool calls/results) into an append-only message log, keyed by session. Supports registration of agent binary fingerprints, session tree tracking (root/parent links), and full session replay. Also provides a `NullStore` no-op implementation for testing.

**Source files:** `src/persistence/persistence_store.h`

**Dependencies:** STL

## 2. Component Specifications

```cpp
namespace a0::persistence {

struct BuildFingerprint {
    std::string binarySha1, repoUrl, commitHash, dirtyHash;
};

struct Message {
    int64_t id, sessionId, createdAt;
    std::string role, content, toolCallsJson;
    std::string toolCallId, name, resultJson;
};

class PersistenceStore {
public:
    virtual ~PersistenceStore() = default;
    virtual int registerAgent(const BuildFingerprint& fp) = 0;
    virtual int64_t createSession(int64_t rootId, int64_t parentId, int agentId) = 0;
    virtual void endSession(int64_t sessionId) = 0;
    virtual int64_t appendMessage(int64_t sessionId, const std::string& role,
        const std::string& content, const std::string& toolCallsJson,
        const std::string& toolCallId, const std::string& name,
        const std::string& resultJson) = 0;
    virtual std::vector<Message> loadMessages(int64_t sessionId) = 0;
    virtual int64_t findSessionByUuid(const std::string& uuid) const = 0;
    virtual void flush() = 0;
};

class NullStore : public PersistenceStore {
    // No-op implementations for testing
};

} // namespace a0::persistence
```

## 3. Architecture

```mermaid
graph TB
    subgraph Interfaces
        PS[PersistenceStore]
        NULL[NullStore]
    end

    PS --> SQLITE[SqliteStore]
    PS --> REPLAY[ReplayEngine]
    AGENT_CORE[AgentCore] --> PS
