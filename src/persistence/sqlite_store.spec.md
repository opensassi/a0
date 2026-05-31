# SqliteStore Spec

## 1. Overview

Concrete SQLite-backed implementation of `PersistenceStore`. Stores agent fingerprints, sessions, and messages in three tables. Uses WAL mode for concurrent read/write performance. Lives under `.a0/db/sessions.db`.

**Source files:** `src/persistence/sqlite_store.h/.cpp`

**Dependencies:** SQLite3, `PersistenceStore`

## 2. Schema

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
    started_at INTEGER NOT NULL, ended_at INTEGER
);

CREATE TABLE message (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES session(id),
    sub_session_id INTEGER,
    seq INTEGER NOT NULL DEFAULT 0,
    role TEXT, content TEXT, tool_calls_json TEXT,
    tool_call_id TEXT, name TEXT, result_json TEXT, created_at INTEGER,
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
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| registerAgent new | Returns unique agent id |
| registerAgent duplicate | Returns existing agent id |
| createSession | Returns session id, uuid set |
| appendMessage then loadMessages | Returns matching message list, sub_session_id and seq preserved |
| flush | WAL checkpoint runs without error |
| createStream + appendChunk + loadStreamChunks | Round-trip: chunks stored and retrieved in order |
| endStream | exit_code set, stream closed |
| ensureSkill new | Returns new skill id |
| ensureSkill duplicate | Returns existing skill id |
| appendInvocation + loadInvocations | Invocation rows stored and retrieved with correct skill/type filter |
