# SqliteStore Spec

## 1. Overview

Concrete SQLite-backed implementation of `PersistenceStore`. Stores agent fingerprints, sessions, messages, streams, invocations, and a task tree in 10 tables. Uses WAL mode for concurrent read/write performance. Lives under `.a0/db/sessions.db`.

**Source files:** `src/persistence/sqlite_store.h/.cpp`

**Dependencies:** SQLite3, `PersistenceStore`

## 2. Component Specifications

```cpp
namespace a0::persistence {

class SqliteStore : public PersistenceStore {
public:
    explicit SqliteStore(const std::string& dbPath);
    ~SqliteStore() override;

    int registerAgent(const BuildFingerprint& fp) override;
    int64_t createSession(const std::string& uuid,
                           int64_t rootId, int64_t parentId,
                           int agentId) override;
    void endSession(int64_t sessionId) override;
    int64_t appendMessage(int64_t sessionId,
                           std::optional<int64_t> subSessionId,
                           int seq, const std::string& role,
                           const std::string& content,
                           const std::string& toolCallsJson,
                           const std::string& toolCallId,
                           const std::string& name,
                           const std::string& resultJson) override;
    std::vector<Message> loadMessages(int64_t sessionId,
                                       std::optional<int64_t> subSessionId = std::nullopt) override;
    int64_t findSessionByUuid(const std::string& uuid) const override;
    std::vector<SessionRow> loadSessions(int limit = 20) const override;
    void flush() override;

    int64_t createStream(int64_t sessionId,
                          const std::string& toolCallId,
                          const std::string& name,
                          const std::string& contextType,
                          const std::string& contextId,
                          const std::string& cwd,
                          const std::string& terminalId = "") override;
    int appendChunk(int64_t streamId, int seq,
                    const std::string& direction,
                    const std::string& data) override;
    int endStream(int64_t streamId, int exitCode) override;
    std::vector<StreamChunk> loadStreamChunks(int64_t streamId,
                                               int offset = 0,
                                               int limit = -1) override;
    std::vector<Stream> listSessionStreams(int64_t sessionId) override;

    int ensureSkill(int type, const std::string& name) override;
    int64_t appendInvocation(int64_t messageId, int skillId,
                              const std::string& toolName,
                              const std::string& paramsJson,
                              const std::string& outputJson) override;
    std::vector<InvocationRow> loadInvocations(int type,
                                                const std::string& name) const override;

    int saveSessionSystemPrompt(int64_t sessionId,
                                 const std::string& systemPrompt,
                                 const std::string& toolDefinitionsJson) override;
    int loadSessionSystemPrompt(int64_t sessionId,
                                 std::string& systemPrompt,
                                 std::string& toolDefinitionsJson) const override;

    int64_t createSessionRootTask(int64_t sessionId) override;
    int64_t getSessionRootTask(int64_t sessionId) const override;
    int64_t addTask(const Task& task) override;
    int removeTask(int64_t taskId) override;
    std::vector<Task> listTasks(int64_t parentTaskId) const override;
    int updateTaskPriority(int64_t taskId, int priority) override;
    Task getTask(int64_t taskId) const override;

    int saveSessionContext(const SessionContextRow& row) override;
    SessionContextRow loadSessionContext(int64_t sessionId) const override;

    void* handle() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::persistence
```

## 3. Schema

```sql
CREATE TABLE agent (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    binary_sha1 TEXT NOT NULL UNIQUE,
    repo_url TEXT, commit_hash TEXT, dirty_hash TEXT, built_at INTEGER
);

CREATE TABLE session (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid TEXT NOT NULL UNIQUE,
    agent_id INTEGER NOT NULL REFERENCES agent(id),
    root_session_id INTEGER REFERENCES session(id),
    parent_session_id INTEGER REFERENCES session(id),
    started_at INTEGER NOT NULL, ended_at INTEGER,
    system_prompt TEXT, tool_definitions TEXT
);

CREATE TABLE message (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES session(id),
    sub_session_id INTEGER,
    seq INTEGER NOT NULL DEFAULT 0,
    role TEXT NOT NULL, content TEXT NOT NULL DEFAULT '',
    tool_calls_json TEXT, tool_call_id TEXT,
    name TEXT, result_json TEXT, created_at INTEGER NOT NULL,
    UNIQUE(session_id, sub_session_id, seq)
);

CREATE INDEX idx_message_session ON message(session_id, id);

CREATE TABLE stream (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES session(id),
    tool_call_id TEXT, terminal_id TEXT,
    name TEXT NOT NULL, context_type TEXT NOT NULL,
    context_id TEXT, cwd TEXT,
    created_at INTEGER NOT NULL, ended_at INTEGER, exit_code INTEGER
);

CREATE TABLE stream_chunk (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    stream_id INTEGER NOT NULL REFERENCES stream(id),
    seq INTEGER NOT NULL, direction TEXT NOT NULL,
    data TEXT NOT NULL, timestamp INTEGER NOT NULL
);

CREATE INDEX idx_stream_session ON stream(session_id);
CREATE INDEX idx_stream_chunk_stream ON stream_chunk(stream_id, seq);
CREATE INDEX idx_stream_tool_call ON stream(tool_call_id);

CREATE TABLE skill (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type INTEGER NOT NULL, name TEXT NOT NULL,
    UNIQUE(type, name)
);

CREATE TABLE invocation (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    message_id INTEGER NOT NULL REFERENCES message(id),
    skill_id INTEGER NOT NULL REFERENCES skill(id),
    tool_name TEXT NOT NULL,
    params_json TEXT, output_json TEXT,
    timestamp INTEGER NOT NULL
);

CREATE INDEX idx_invocation_skill ON invocation(skill_id, tool_name);
CREATE INDEX idx_invocation_message ON invocation(message_id);

CREATE TABLE session_context (
    session_id INTEGER PRIMARY KEY REFERENCES session(id),
    session_uuid TEXT DEFAULT '', original_cwd TEXT DEFAULT '',
    worktree_path TEXT DEFAULT '', git_repo_root TEXT DEFAULT '',
    git_branch TEXT DEFAULT '', git_commit TEXT DEFAULT ''
);

CREATE TABLE task (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    root_task_id INTEGER NOT NULL,
    parent_task_id INTEGER NOT NULL,
    session_id INTEGER NOT NULL,
    description TEXT NOT NULL,
    detailed_plan TEXT NOT NULL DEFAULT '',
    automated_verification TEXT NOT NULL DEFAULT '',
    human_verification TEXT NOT NULL DEFAULT '',
    priority INTEGER NOT NULL DEFAULT 0,
    status TEXT NOT NULL DEFAULT 'pending',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE INDEX idx_task_parent ON task(parent_task_id);
CREATE INDEX idx_task_root ON task(root_task_id);

CREATE TABLE session_tasks (
    session_id INTEGER PRIMARY KEY,
    root_task_id INTEGER NOT NULL
);
```

## 4. Migration Notes

The store runs the following migrations on existing databases:

- Adds `terminal_id TEXT` column to `stream` table (safe to re-run)
- Adds `system_prompt TEXT` and `tool_definitions TEXT` columns to `session` table
- Adds `sub_session_id INTEGER` and `seq INTEGER NOT NULL DEFAULT 0` columns to `message` table, then backfills `seq` based on insertion order within each session
- Creates `task`, `session_tasks`, `session_context` tables if not present

All migrations use `ALTER TABLE ... ADD COLUMN` or `CREATE TABLE IF NOT EXISTS`.

## 5. Testing Requirements

| Test | Verification |
|------|-------------|
| registerAgent new | Returns unique agent id |
| registerAgent duplicate | Returns existing agent id |
| createSession | Returns session id, provided uuid stored |
| appendMessage then loadMessages | Round-trip preserves sub_session_id and seq |
| flush | WAL checkpoint runs without error |
| createStream + chunks | Round-trip: chunks stored and retrieved in order |
| endStream | exit_code set, stream closed |
| ensureSkill new | Returns new skill id |
| ensureSkill duplicate | Returns existing skill id |
| appendInvocation + loadInvocations | Round-trip with correct skill/type filter |
| saveSessionContext + loadSessionContext | All fields preserved |
| loadSessionContext unknown | Returns empty SessionContextRow |
| createSessionRootTask | Root task created, self-referencing, linked in session_tasks |
| addTask + getTask | Round-trip: all fields preserved |
| removeTask leaf | Removed, getTask returns default |
| removeTask with children | Returns -1, task preserved |
| listTasks | Children ordered by priority then id |
| updateTaskPriority | Priority updated |
| saveSessionSystemPrompt + loadSessionSystemPrompt | System prompt and tool definitions round-trip |
| loadSessions | Returns sessions ordered by started_at DESC |
| handle() | Returns non-null pointer |
