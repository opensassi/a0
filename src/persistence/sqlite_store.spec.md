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
    role TEXT, content TEXT, tool_calls_json TEXT,
    tool_call_id TEXT, name TEXT, result_json TEXT, created_at INTEGER
);

CREATE INDEX idx_message_session ON message(session_id, id);
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| registerAgent new | Returns unique agent id |
| registerAgent duplicate | Returns existing agent id |
| createSession | Returns session id, uuid set |
| appendMessage then loadMessages | Returns matching message list |
| flush | WAL checkpoint runs without error |
