#include "sqlite_store.h"
#include <sqlite3.h>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <sstream>
#include <vector>

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

        // Enable WAL mode
        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA foreign_keys=ON");
        exec(
            "CREATE TABLE IF NOT EXISTS agent ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  binary_sha1 TEXT NOT NULL UNIQUE,"
            "  repo_url TEXT,"
            "  commit_hash TEXT,"
            "  dirty_hash TEXT,"
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
            "  role TEXT NOT NULL,"
            "  content TEXT NOT NULL DEFAULT '',"
            "  tool_calls_json TEXT,"
            "  tool_call_id TEXT,"
            "  name TEXT,"
            "  result_json TEXT,"
            "  created_at INTEGER NOT NULL"
            ")"
        );
        exec(
            "CREATE INDEX IF NOT EXISTS idx_message_session"
            "  ON message(session_id, id)"
        );
    }

    ~Impl() {
        if (db) {
            sqlite3_close(db);
        }
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

int64_t SqliteStore::createSession(int64_t rootId, int64_t parentId, int agentId)
{
    sqlite3_stmt* stmt;
    // Generate UUID
    std::ostringstream uuid;
    uuid << "ses_" << std::time(nullptr) << "_" << rootId;

    const char* sql = "INSERT INTO session (uuid, agent_id, root_session_id, parent_session_id, started_at)"
          " VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(m_impl->db));
    }
    sqlite3_bind_text(stmt, 1, uuid.str().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, agentId);
    sqlite3_bind_int64(stmt, 3, rootId);
    sqlite3_bind_int64(stmt, 4, parentId);
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
    const std::string& role,
    const std::string& content,
    const std::string& toolCallsJson,
    const std::string& toolCallId,
    const std::string& name,
    const std::string& resultJson)
{
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO message"
          " (session_id, role, content, tool_calls_json, tool_call_id, name, result_json, created_at)"
          " VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, sessionId);
    sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, toolCallsJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, toolCallId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, resultJson.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, static_cast<sqlite3_int64>(std::time(nullptr)));

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int64_t id = sqlite3_last_insert_rowid(m_impl->db);
    sqlite3_finalize(stmt);
    return id;
}

std::vector<Message> SqliteStore::loadMessages(int64_t sessionId)
{
    std::vector<Message> messages;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, session_id, role, content, tool_calls_json,"
          " tool_call_id, name, result_json, created_at"
          " FROM message WHERE session_id = ? ORDER BY id";
    if (sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return messages;
    }
    sqlite3_bind_int64(stmt, 1, sessionId);

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
        messages.push_back(msg);
    }
    sqlite3_finalize(stmt);
    return messages;
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

} // namespace a0::persistence
