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
    int64_t subSessionId = 0;
    int seq = 0;
};

struct Stream {
    int64_t id, sessionId;
    std::string toolCallId, terminalId, name, contextType, contextId, cwd;
    int64_t createdAt, endedAt;
    int exitCode;
};

struct StreamChunk {
    int64_t id, streamId;
    int seq;
    std::string direction, data;
    int64_t timestamp;
};

struct InvocationRow {
    int64_t id, messageId, skillId;
    std::string toolName, paramsJson, outputJson;
    int64_t timestamp;
};

struct SessionContextRow {
    int64_t sessionId;
    std::string sessionUuid;
    std::string originalCwd;
    std::string worktreePath;
    std::string gitRepoRoot;
    std::string gitBranch;
    std::string gitCommit;
};

class PersistenceStore {
public:
    virtual ~PersistenceStore() = default;
    virtual int registerAgent(const BuildFingerprint& fp) = 0;
    virtual int64_t createSession(const std::string& uuid,
                                   int64_t rootId,
                                   int64_t parentId,
                                   int agentId) = 0;
    virtual void endSession(int64_t sessionId) = 0;
    virtual int64_t appendMessage(int64_t sessionId,
                                   std::optional<int64_t> subSessionId,
                                   int seq,
                                   const std::string& role,
                                   const std::string& content,
                                   const std::string& toolCallsJson,
                                   const std::string& toolCallId,
                                   const std::string& name,
                                   const std::string& resultJson) = 0;
    virtual std::vector<Message> loadMessages(int64_t sessionId,
                                               std::optional<int64_t> subSessionId = std::nullopt) = 0;
    virtual int64_t findSessionByUuid(const std::string& uuid) const = 0;
    virtual void flush() = 0;

    // --- Streaming ---
    virtual int64_t createStream(int64_t sessionId,
                                  const std::string& toolCallId,
                                  const std::string& name,
                                  const std::string& contextType,
                                  const std::string& contextId,
                                  const std::string& cwd,
                                  const std::string& terminalId = "") = 0;
    virtual int appendChunk(int64_t streamId, int seq,
                             const std::string& direction,
                             const std::string& data) = 0;
    virtual int endStream(int64_t streamId, int exitCode) = 0;
    virtual std::vector<StreamChunk> loadStreamChunks(int64_t streamId,
                                                       int offset = 0,
                                                       int limit = -1) = 0;
    virtual std::vector<Stream> listSessionStreams(int64_t sessionId) = 0;

    // --- Skill invocation tracking ---
    virtual int ensureSkill(int type, const std::string& name) = 0;
    virtual int64_t appendInvocation(int64_t messageId,
                                      int skillId,
                                      const std::string& toolName,
                                      const std::string& paramsJson,
                                      const std::string& outputJson) = 0;
    virtual std::vector<InvocationRow> loadInvocations(int type,
                                                        const std::string& name) const = 0;

    // --- Session context ---
    virtual int saveSessionContext(const SessionContextRow& row) = 0;
    virtual SessionContextRow loadSessionContext(int64_t sessionId) const = 0;
};

class NullStore : public PersistenceStore {
    // No-op implementations for testing (all methods return 0 or empty)
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
