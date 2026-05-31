#include "sqlite_store.h"
#include <sqlite3.h>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <sstream>
#include <vector>
#include <algorithm>

namespace a0::persistence {

class SqliteStore::Impl {
public:
    sqlite3* db = nullptr;

    Impl(const std::string& dbPath) {
        std::string dir = dbPath.substr(0, dbPath.rfind('/'));
        if (!dir.empty()) {
            std::string mkdir = "mkdir -p " + dir;
            ::system(mkdir.c_str());
        }

        int rc = sqlite3_open(dbPath.c_str(), &db);
        if (rc != SQLITE_OK) {
            std::string err = "failed to open database: ";
            err += sqlite3_errmsg(db);
            if (db) sqlite3_close(db);
            throw std::runtime_error(err);
        }

        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA foreign_keys=ON");
        exec(
            "CREATE TABLE IF NOT EXISTS agent ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  binary_sha1 TEXT NOT NULL UNIQUE,"
            "  repo_url TEXT, commit_hash TEXT, dirty_hash TEXT,"
            "  built_at INTEGER"
            ")"
        );
        exec(
            "CREATE TABLE IF NOT EXISTS session ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  uuid TEXT NOT NULL UNIQUE,"
            "  agent_id INTEGER NOT NULL REFERENCES agent(id),"
            "  root_session_id INTEGER REFERENCES session(id),"
            "  parent_session_id INTEGER REFERENCES session(id),"
            "  started_at INTEGER NOT NULL,"
            "  ended_at INTEGER"
            ")"
        );
        exec(
            "CREATE TABLE IF NOT EXISTS message ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id INTEGER NOT NULL REFERENCES session(id),"
            "  sub_session_id INTEGER,"
            "  seq INTEGER NOT NULL DEFAULT 0,"
            "  role TEXT NOT NULL,"
            "  content TEXT NOT NULL DEFAULT '',"
            "  tool_calls_json TEXT,"
            "  tool_call_id TEXT,"
            "  name TEXT,"
            "  result_json TEXT,"
            "  created_at INTEGER NOT NULL,"
            "  UNIQUE(session_id, sub_session_id, seq)"
            ")"
        );
        exec(
            "CREATE INDEX IF NOT EXISTS idx_message_session"
            "  ON message(session_id, id)"
        );
        exec(
            "CREATE TABLE IF NOT EXISTS stream ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id INTEGER NOT NULL REFERENCES session(id),"
            "  tool_call_id TEXT, terminal_id TEXT,"
            "  name TEXT NOT NULL, context_type TEXT NOT NULL,"
            "  context_id TEXT, cwd TEXT,"
            "  created_at INTEGER NOT NULL, ended_at INTEGER, exit_code INTEGER"
            ")"
        );
        exec(
            "CREATE TABLE IF NOT EXISTS stream_chunk ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  stream_id INTEGER NOT NULL REFERENCES stream(id),"
            "  seq INTEGER NOT NULL, direction TEXT NOT NULL,"
            "  data TEXT NOT NULL, timestamp INTEGER NOT NULL"
            ")"
        );
        exec("CREATE INDEX IF NOT EXISTS idx_stream_session ON stream(session_id)");
        exec("CREATE INDEX IF NOT EXISTS idx_stream_chunk_stream ON stream_chunk(stream_id, seq)");
        exec("CREATE INDEX IF NOT EXISTS idx_stream_tool_call ON stream(tool_call_id)");
        exec("CREATE TABLE IF NOT EXISTS skill ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  type INTEGER NOT NULL,"
            "  name TEXT NOT NULL,"
            "  UNIQUE(type, name)"
            ")");
        exec("CREATE TABLE IF NOT EXISTS invocation ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  message_id INTEGER NOT NULL REFERENCES message(id),"
            "  skill_id INTEGER NOT NULL REFERENCES skill(id),"
            "  tool_name TEXT NOT NULL,"
            "  params_json TEXT,"
            "  output_json TEXT,"
            "  timestamp INTEGER NOT NULL"
            ")");
        exec("CREATE INDEX IF NOT EXISTS idx_invocation_skill ON invocation(skill_id, tool_name)");
        exec("CREATE INDEX IF NOT EXISTS idx_invocation_message ON invocation(message_id)");

        // Migration: add terminal_id column for existing databases
        {
            char* err = nullptr;
            sqlite3_exec(db, "ALTER TABLE stream ADD COLUMN terminal_id TEXT",
                         nullptr, nullptr, &err);
            sqlite3_free(err);
        }
        // Migration: add sub_session_id + seq for existing databases
        {
            char* err = nullptr;
            int rc2 = sqlite3_exec(db, "ALTER TABLE message ADD COLUMN sub_session_id INTEGER",
                                    nullptr, nullptr, &err);
            if (rc2 == SQLITE_OK) {
                sqlite3_exec(db, "ALTER TABLE message ADD COLUMN seq INTEGER NOT NULL DEFAULT 0",
                             nullptr, nullptr, nullptr);
                // Backfill seq for existing rows (ordered by session_id, id)
                sqlite3_exec(db,
                    "UPDATE message SET seq = ("
                    "  SELECT COUNT(*) FROM message AS m2"
                    "  WHERE m2.session_id = message.session_id"
                    "  AND m2.id <= message.id"
                    ")", nullptr, nullptr, nullptr);
            }
            sqlite3_free(err);
        }
    }

    ~Impl() {
        if (db) sqlite3_close(db);
    }

    void exec(const std::string& sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("sqlite exec: " + msg);
        }
    }
};

SqliteStore::SqliteStore(const std::string& dbPath)
    : m_impl(std::make_unique<Impl>(dbPath)) {}

SqliteStore::~SqliteStore() = default;

int SqliteStore::registerAgent(const BuildFingerprint& fp)
{
    // Check if exists
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM agent WHERE binary_sha1 = ?";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(m_impl->db));
    }
    sqlite3_bind_text(stmt, 1, fp.binarySha1.c_str(), -1, SQLITE_TRANSIENT);

    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    if (id > 0) return id;

    // Insert
    sql = "INSERT INTO agent (binary_sha1, repo_url, commit_hash, dirty_hash, built_at)"
          " VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(m_impl->db));
    }
    sqlite3_bind_text(stmt, 1, fp.binarySha1.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, fp.repoUrl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, fp.commitHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fp.dirtyHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(std::time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error(sqlite3_errmsg(m_impl->db));
    }
    id = static_cast<int>(sqlite3_last_insert_rowid(m_impl->db));
    sqlite3_finalize(stmt);
    return id;
}

int64_t SqliteStore::createSession(const std::string& uuid, int64_t rootId, int64_t parentId, int agentId)
{
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO session (uuid, agent_id, root_session_id, parent_session_id, started_at)"
          " VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(m_impl->db));
    }
    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, agentId);
    if (rootId > 0)
        sqlite3_bind_int64(stmt, 3, rootId);
    else
        sqlite3_bind_null(stmt, 3);
    if (parentId > 0)
        sqlite3_bind_int64(stmt, 4, parentId);
    else
        sqlite3_bind_null(stmt, 4);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(std::time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error(sqlite3_errmsg(m_impl->db));
    }
    int64_t id = sqlite3_last_insert_rowid(m_impl->db);
    sqlite3_finalize(stmt);
    return id;
}

void SqliteStore::endSession(int64_t sessionId)
{
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE session SET ended_at = ? WHERE id = ?";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(std::time(nullptr)));
    sqlite3_bind_int64(stmt, 2, sessionId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int64_t SqliteStore::appendMessage(
    int64_t sessionId,
    std::optional<int64_t> subSessionId,
    int seq,
    const std::string& role,
    const std::string& content,
    const std::string& toolCallsJson,
    const std::string& toolCallId,
    const std::string& name,
    const std::string& resultJson)
{
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO message"
          " (session_id, sub_session_id, seq, role, content, tool_calls_json,"
          "  tool_call_id, name, result_json, created_at)"
          " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, sessionId);
    if (subSessionId.has_value())
        sqlite3_bind_int64(stmt, 2, subSessionId.value());
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_int(stmt, 3, seq);
    sqlite3_bind_text(stmt, 4, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, toolCallsJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, toolCallId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, resultJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 10, static_cast<sqlite3_int64>(std::time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t id = sqlite3_last_insert_rowid(m_impl->db);
    sqlite3_finalize(stmt);
    return id;
}

std::vector<Message> SqliteStore::loadMessages(int64_t sessionId,
                                                std::optional<int64_t> subSessionId)
{
    std::vector<Message> messages;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, session_id, role, content, tool_calls_json,"
          " tool_call_id, name, result_json, created_at,"
          " COALESCE(sub_session_id, 0), seq"
          " FROM message WHERE session_id = ?";
    if (!subSessionId.has_value()) {
        sql += " AND sub_session_id IS NULL";
    } else if (subSessionId.value() >= 0) {
        sql += " AND COALESCE(sub_session_id, 0) = ?";
    }
    sql += " ORDER BY seq";
    if (sqlite3_prepare_v2(m_impl->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return messages;
    }
    sqlite3_bind_int64(stmt, 1, sessionId);
    int bindIdx = 2;
    if (subSessionId.has_value() && subSessionId.value() >= 0) {
        sqlite3_bind_int64(stmt, bindIdx++, subSessionId.value());
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message msg;
        msg.id = sqlite3_column_int64(stmt, 0);
        msg.sessionId = sqlite3_column_int64(stmt, 1);
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)))
            msg.role = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)))
            msg.content = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
            msg.toolCallsJson = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)))
            msg.toolCallId = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)))
            msg.name = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)))
            msg.resultJson = s;
        msg.createdAt = sqlite3_column_int64(stmt, 8);
        msg.subSessionId = sqlite3_column_int64(stmt, 9);
        msg.seq = sqlite3_column_int(stmt, 10);
        messages.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return messages;
}

int SqliteStore::ensureSkill(int type, const std::string& name)
{
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM skill WHERE type = ? AND name = ?";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, type);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    if (id > 0) return id;

    const char* ins = "INSERT INTO skill (type, name) VALUES (?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, ins, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, type);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    id = static_cast<int>(sqlite3_last_insert_rowid(m_impl->db));
    sqlite3_finalize(stmt);
    return id;
}

int64_t SqliteStore::appendInvocation(int64_t messageId,
                                       int skillId,
                                       const std::string& toolName,
                                       const std::string& paramsJson,
                                       const std::string& outputJson)
{
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO invocation"
          " (message_id, skill_id, tool_name, params_json, output_json, timestamp)"
          " VALUES (?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, messageId);
    sqlite3_bind_int(stmt, 2, skillId);
    sqlite3_bind_text(stmt, 3, toolName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, paramsJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, outputJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(std::time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t id = sqlite3_last_insert_rowid(m_impl->db);
    sqlite3_finalize(stmt);
    return id;
}

std::vector<InvocationRow> SqliteStore::loadInvocations(int type,
                                                         const std::string& name) const
{
    std::vector<InvocationRow> rows;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT inv.id, inv.message_id, inv.skill_id,"
          " inv.tool_name, inv.params_json, inv.output_json, inv.timestamp"
          " FROM invocation inv"
          " JOIN skill sk ON sk.id = inv.skill_id"
          " WHERE sk.type = ? AND sk.name = ?"
          " ORDER BY inv.timestamp";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return rows;
    }
    sqlite3_bind_int(stmt, 1, type);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        InvocationRow r;
        r.id = sqlite3_column_int64(stmt, 0);
        r.messageId = sqlite3_column_int64(stmt, 1);
        r.skillId = sqlite3_column_int64(stmt, 2);
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)))
            r.toolName = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
            r.paramsJson = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)))
            r.outputJson = s;
        r.timestamp = sqlite3_column_int64(stmt, 6);
        rows.push_back(r);
    }
    sqlite3_finalize(stmt);
    return rows;
}

int64_t SqliteStore::findSessionByUuid(const std::string& uuid) const
{
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM session WHERE uuid = ?";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
    int64_t id = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return id;
}

void SqliteStore::flush()
{
    m_impl->exec("PRAGMA wal_checkpoint(TRUNCATE)");
}

int64_t SqliteStore::createStream(
    int64_t sessionId,
    const std::string& toolCallId,
    const std::string& name,
    const std::string& contextType,
    const std::string& contextId,
    const std::string& cwd,
    const std::string& terminalId)
{
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO stream"
        " (session_id, tool_call_id, terminal_id, name, context_type, context_id, cwd, created_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, sessionId);
    if (toolCallId.empty())
        sqlite3_bind_null(stmt, 2);
    else
        sqlite3_bind_text(stmt, 2, toolCallId.c_str(), -1, SQLITE_TRANSIENT);
    if (terminalId.empty())
        sqlite3_bind_null(stmt, 3);
    else
        sqlite3_bind_text(stmt, 3, terminalId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, contextType.c_str(), -1, SQLITE_TRANSIENT);
    if (contextId.empty())
        sqlite3_bind_null(stmt, 6);
    else
        sqlite3_bind_text(stmt, 6, contextId.c_str(), -1, SQLITE_TRANSIENT);
    if (cwd.empty())
        sqlite3_bind_null(stmt, 7);
    else
        sqlite3_bind_text(stmt, 7, cwd.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(std::time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t id = sqlite3_last_insert_rowid(m_impl->db);
    sqlite3_finalize(stmt);
    return id;
}

int SqliteStore::appendChunk(
    int64_t streamId, int seq,
    const std::string& direction,
    const std::string& data)
{
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO stream_chunk"
        " (stream_id, seq, direction, data, timestamp)"
        " VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, streamId);
    sqlite3_bind_int(stmt, 2, seq);
    sqlite3_bind_text(stmt, 3, direction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, data.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(std::time(nullptr)));

    int rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(stmt);
    return rc;
}

int SqliteStore::endStream(int64_t streamId, int exitCode)
{
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE stream SET ended_at = ?, exit_code = ? WHERE id = ?";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(std::time(nullptr)));
    sqlite3_bind_int(stmt, 2, exitCode);
    sqlite3_bind_int64(stmt, 3, streamId);

    int rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(stmt);
    return rc;
}

std::vector<StreamChunk> SqliteStore::loadStreamChunks(
    int64_t streamId, int offset, int limit)
{
    std::vector<StreamChunk> chunks;
    sqlite3_stmt* stmt;
    std::string sql = "SELECT id, stream_id, seq, direction, data, timestamp"
        " FROM stream_chunk WHERE stream_id = ? ORDER BY seq";
    if (offset > 0 || limit > 0) {
        sql += " LIMIT ? OFFSET ?";
    }
    if (sqlite3_prepare_v2(m_impl->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return chunks;
    }
    sqlite3_bind_int64(stmt, 1, streamId);
    if (offset > 0 || limit > 0) {
        sqlite3_bind_int(stmt, 2, limit > 0 ? limit : 1000);
        sqlite3_bind_int(stmt, 3, offset);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        StreamChunk c;
        c.id = sqlite3_column_int64(stmt, 0);
        c.streamId = sqlite3_column_int64(stmt, 1);
        c.seq = sqlite3_column_int(stmt, 2);
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)))
            c.direction = s;
        if (auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
            c.data = s;
        c.timestamp = sqlite3_column_int64(stmt, 5);
        chunks.push_back(c);
    }
    sqlite3_finalize(stmt);
    return chunks;
}

std::vector<Stream> SqliteStore::listSessionStreams(int64_t sessionId)
{
    std::vector<Stream> streams;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, session_id, tool_call_id, terminal_id, name, context_type,"
        " context_id, cwd, created_at, ended_at, exit_code"
        " FROM stream WHERE session_id = ? ORDER BY created_at";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return streams;
    }
    sqlite3_bind_int64(stmt, 1, sessionId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Stream s;
        s.id = sqlite3_column_int64(stmt, 0);
        s.sessionId = sqlite3_column_int64(stmt, 1);
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)))
            s.toolCallId = p;
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)))
            s.terminalId = p;
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
            s.name = p;
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)))
            s.contextType = p;
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)))
            s.contextId = p;
        if (auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7)))
            s.cwd = p;
        s.createdAt = sqlite3_column_int64(stmt, 8);
        if (sqlite3_column_type(stmt, 9) == SQLITE_NULL)
            s.endedAt = 0;
        else
            s.endedAt = sqlite3_column_int64(stmt, 9);
        if (sqlite3_column_type(stmt, 10) == SQLITE_NULL)
            s.exitCode = -1;
        else
            s.exitCode = sqlite3_column_int(stmt, 10);
        streams.push_back(s);
    }
    sqlite3_finalize(stmt);
    return streams;
}

void* SqliteStore::handle() const {
    return m_impl->db;
}

} // namespace a0::persistence
