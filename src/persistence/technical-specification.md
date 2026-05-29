# Technical Specification: Persistence Sub-Module

## For a0 Agent — Version 1.0

---

## 1. Overview

The Persistence sub-module records every agent invocation into a SQLite database for crash reproduction and deterministic replay. It captures the full conversational loop: user input → LLM request → tool execution → LLM response.

**Goals:**

- Record every agent I/O into an append-only message log
- Fingerprint the agent binary by SHA1 so replay uses the correct version
- Support deterministic replay: inject stored LLM responses, re-execute tools, compare outputs
- Enable sub-agent tracing via parent/root session links
- Provide a clean abstract interface so SQLite can be swapped out

**Dependencies:** SQLite3 (default), `CommandRunner` (for replay execution), `BuildIdentity` (for binary fingerprint)

**Lifecycle:** Per-session. One database per project root, located at `a0Dir + "/db/sessions.db"` where `a0Dir` defaults to `./.a0/` and is configurable via the `--a0-dir` CLI flag. The `a0Dir` directory is auto-created on agent startup by `ensureA0Dir()`.

---

## 2. Data Structures

### 2.1 SQLite Schema

```sql
CREATE TABLE agent (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    binary_sha1 TEXT NOT NULL UNIQUE,
    repo_url TEXT,
    commit_hash TEXT,
    dirty_hash TEXT,
    built_at INTEGER
);

CREATE TABLE session (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid TEXT NOT NULL UNIQUE,
    agent_id INTEGER NOT NULL REFERENCES agent(id),
    root_session_id INTEGER REFERENCES session(id),
    parent_session_id INTEGER REFERENCES session(id),
    started_at INTEGER NOT NULL,
    ended_at INTEGER
);

CREATE TABLE message (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES session(id),
    role TEXT NOT NULL,
    content TEXT NOT NULL DEFAULT '',
    tool_calls_json TEXT,
    tool_call_id TEXT,
    name TEXT,
    result_json TEXT,
    created_at INTEGER NOT NULL
);

CREATE INDEX idx_message_session ON message(session_id, id);
```

### 2.2 Abstract Interface

```cpp
namespace a0::persistence {

struct BuildFingerprint {
    std::string binarySha1;
    std::string repoUrl;
    std::string commitHash;
    std::string dirtyHash;
};

struct Message {
    int64_t id;
    int64_t sessionId;
    std::string role;
    std::string content;
    std::string toolCallsJson;
    std::string toolCallId;
    std::string name;
    std::string resultJson;
    int64_t createdAt;
};

class PersistenceStore {
public:
    virtual ~PersistenceStore() = default;

    virtual int registerAgent(const BuildFingerprint& fp) = 0;

    virtual int64_t createSession(int64_t rootId,
                                   int64_t parentId,
                                   int agentId) = 0;
    virtual void endSession(int64_t sessionId) = 0;

    virtual int64_t appendMessage(int64_t sessionId,
                                   const std::string& role,
                                   const std::string& content,
                                   const std::string& toolCallsJson,
                                   const std::string& toolCallId,
                                   const std::string& name,
                                   const std::string& resultJson) = 0;

    virtual std::vector<Message> loadMessages(int64_t sessionId) = 0;
    virtual int64_t findSessionByUuid(const std::string& uuid) const = 0;
    virtual void flush() = 0;
};

} // namespace a0::persistence
```

### 2.3 SQLite Store

```cpp
namespace a0::persistence {

class SqliteStore : public PersistenceStore {
public:
    explicit SqliteStore(const std::string& dbPath);
    ~SqliteStore() override;

    int registerAgent(const BuildFingerprint& fp) override;
    int64_t createSession(int64_t rootId, int64_t parentId, int agentId) override;
    void endSession(int64_t sessionId) override;
    int64_t appendMessage(int64_t sessionId,
                           const std::string& role,
                           const std::string& content,
                           const std::string& toolCallsJson,
                           const std::string& toolCallId,
                           const std::string& name,
                           const std::string& resultJson) override;
    std::vector<Message> loadMessages(int64_t sessionId) override;
    int64_t findSessionByUuid(const std::string& uuid) const override;
    void flush() override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::persistence
```

### 2.4 Replay Engine

```cpp
namespace a0::persistence {

class ReplayEngine {
public:
    explicit ReplayEngine(PersistenceStore* store);

    int replay(int64_t sessionId, std::string& divergence);
    int replayTo(int64_t sessionId, int64_t upToMessageId, std::string& divergence);

private:
    PersistenceStore* m_store;

    int xInjectAssistant(const Message& msg);
    int xExecuteTool(const Message& msg, std::string& actualResult);
    int xCompareTool(const std::string& expected,
                     const std::string& actual,
                     std::string& divergence);
};

} // namespace a0::persistence
```

### 2.5 Build Identity

```cpp
namespace a0::persistence {

class BuildIdentity {
public:
    static std::string binarySha1();
    static void detectGit(const std::string& projectDir, BuildFingerprint& fp);
};

} // namespace a0::persistence
```

---

## 3. System Architecture

```mermaid
graph TB
    subgraph "Agent"
        AC[AgentCore]
        DP[DeepSeekProvider]
        CR[CommandRunner]
    end

    subgraph "Persistence Sub-module"
        PS[PersistenceStore]
        SS[SqliteStore]
        RE[ReplayEngine]
    end

    subgraph "Storage"
        DB[(.a0/db/sessions.db)]
    end

    subgraph "Build Identity"
        BI[BuildIdentity]
    end

    AC --> PS
    DP --> PS
    CR --> PS
    PS --> SS
    SS --> DB
    RE --> SS
    BI --> PS
```

---

## 4. Data Flow

### 4.1 Normal Operation

```mermaid
sequenceDiagram
    participant AC as AgentCore
    participant DP as DeepSeekProvider
    participant CR as CommandRunner
    participant PS as PersistenceStore
    participant DB as SQLite

    AC->>PS: createSession(root, parent, agentId)
    PS->>DB: INSERT INTO session
    DB-->>PS: id

    AC->>PS: appendMessage("user", content, ...)
    PS->>DB: INSERT INTO message
    DB-->>PS: id

    AC->>DP: complete(systemPrompt, userPrompt)
    DP->>DP: call LLM API
    DP-->>AC: {tool_calls: [...]}

    AC->>PS: appendMessage("assistant", "", toolCallsJson)
    PS->>DB: INSERT INTO message

    AC->>CR: run(cmd, stdin, timeout)
    CR-->>AC: result

    AC->>PS: appendMessage("tool", "", "", callId, name, resultJson)
    PS->>DB: INSERT INTO message
```

### 4.2 Deterministic Replay

```mermaid
sequenceDiagram
    participant RE as ReplayEngine
    participant PS as PersistenceStore
    participant AC as AgentCore
    participant CR as CommandRunner

    RE->>PS: loadMessages(sessionId)
    PS->>RE: [user, assistant, tool, assistant, ...]

    loop each message
        alt role == "assistant"
            RE->>RE: inject stored LLM response
            RE->>RE: compare content + tool_calls
        else role == "tool"
            RE->>CR: re-execute tool
            CR-->>RE: actual output
            RE->>RE: compare actual vs stored resultJson
            RE->>AC: feed actual result
        else role == "user"
            RE->>AC: feed stored user input
        end
    end

    alt divergence found
        RE-->>User: divergence at message N
    else all match
        RE-->>User: replay succeeded
    end
```

---

## 5. Error Handling

| Scenario | Behaviour |
|----------|-----------|
| DB file does not exist | Created automatically on first `createSession` |
| DB file is corrupt | `SqliteStore` constructor throws `std::runtime_error` |
| WAL mode write contention | SQLite WAL handles concurrent reads + single writer |
| `appendMessage` after session ended | Returns -1 |
| `loadMessages` with nonexistent session | Returns empty vector |
| Replay: session not found | Returns -1 |
| Replay: tool re-execution fails | Divergence captured with crash output |
| `binarySha1` cannot read self | Returns empty string |

## 6. Edge Cases

| Case | Expected Result |
|------|----------------|
| Two agents run simultaneously | Separate sessions, WAL handles concurrency |
| Sub-agent spawned recursively | Child session with parent_session_id set |
| Agent crash mid-session | ended_at NULL; replay still works |
| DB deleted mid-session | Next appendMessage fails |
| Very long LLM response (>1 MB) | Stored as-is in content column |
| Binary rebuilt between runs | New SHA1 → new agent row |
| No git repo detected | repo_url/commit_hash stored as NULL |

## 7. Testing Requirements

### SqliteStore

| Method | Test Case | Expected |
|--------|-----------|----------|
| `registerAgent` | New fingerprint | Returns new id |
| `registerAgent` | Duplicate | Returns existing id |
| `createSession` | Root session | root_id = id, parent_id = 0 |
| `createSession` | Sub-session | parent_id set |
| `endSession` | Existing session | ended_at set |
| `appendMessage` | All four roles | Four rows with correct data |
| `loadMessages` | Session with 10 messages | 10 messages in order |
| `loadMessages` | Nonexistent | Empty vector |

### ReplayEngine

| Method | Test Case | Expected |
|--------|-----------|----------|
| `replay` | All tool results match | 0 |
| `replay` | Tool result differs | 1 |
| `replay` | Tool re-execution crashes | 1 with crash output |
| `replayTo` | Target message found | 0 |
| `replayTo` | Divergence before target | 1 |

### BuildIdentity

| Method | Test Case | Expected |
|--------|-----------|----------|
| `binarySha1` | Normal binary | 40-char hex |
| `binarySha1` | Binary not readable | Empty string |
| `detectGit` | Clean repo | commitHash set, dirtyHash empty |
| `detectGit` | Dirty repo | dirtyHash non-empty |
| `detectGit` | No git repo | All empty |

## 8. CLI Entry Point

```
a0 replay --session <id>
    Replay a stored session against the current binary.
    Re-executes tools, compares outputs, reports first divergence.

a0 replay --session <id> --step <message-id>
    Replay up to a specific message and stop.
```

The `--root` and `--parent` flags flow into `createSession`:

```
a0 --root <session-id> --parent <session-id> [other flags]
    Spawns a sub-agent. Creates child session linked to parent.
```

## 9. Integration

Wire-up in `main.cpp`:

1. `ensureA0Dir(a0Dir)` creates `./.a0/` (or the configured `--a0-dir` path) on startup
2. `BuildIdentity::binarySha1()` + `BuildIdentity::detectGit()` → register `agent` row
3. `SqliteStore` constructed with `a0Dir + "/db/sessions.db"` during startup
4. `AgentCore` receives `PersistenceStore*`
5. `DeepSeekProvider::complete()` records `role=assistant`
6. `CommandRunner::run()` records `role=tool`
7. `--root` and `--parent` CLI flags populate `createSession` params
8. `a0 replay` reads DB and drives the agent deterministically

## 10. Implementation Outline

### Phase 1: BuildIdentity + SqliteStore

- Implement `binarySha1()` via reading `/proc/self/exe`
- Implement `detectGit()` via `git rev-parse HEAD`
- Implement `SqliteStore` — schema creation, CRUD for agent/session/message
- Use WAL mode for concurrent sub-agent access

### Phase 2: Instrumentation

- Wire `PersistenceStore*` through `AgentCore` → `DeepSeekProvider` → `CommandRunner`
- Record messages at each call site
- Handle `--root` / `--parent` in `main.cpp`

### Phase 3: ReplayEngine

- Implement message-driven replay loop
- Tool re-execution via `CommandRunner`
- Output comparison + divergence reporting
