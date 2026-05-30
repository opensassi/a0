#include "c2_event_store.h"
#include <sqlite3.h>
#include <chrono>
#include <iostream>

namespace a0::c2 {

class EventStore::Impl {
public:
    sqlite3* db = nullptr;

    explicit Impl(const std::string& dbPath) {
        int rc = sqlite3_open(dbPath.c_str(), &db);
        if (rc != SQLITE_OK) {
            std::cerr << "c2: failed to open event db: " << sqlite3_errmsg(db) << "\n";
            db = nullptr;
            return;
        }
        const char* sql =
            "CREATE TABLE IF NOT EXISTS pending_prompts ("
            "  session      TEXT PRIMARY KEY,"
            "  tool_call_id TEXT NOT NULL,"
            "  prompt       TEXT NOT NULL,"
            "  context      TEXT DEFAULT '',"
            "  created_at   INTEGER NOT NULL"
            ")";
        char* err = nullptr;
        rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::cerr << "c2: event db init: " << err << "\n";
            sqlite3_free(err);
        }
    }

    ~Impl() {
        if (db) sqlite3_close(db);
    }
};

EventStore::EventStore(const std::string& dbPath)
    : m_impl(std::make_unique<Impl>(dbPath)) {}

EventStore::~EventStore() = default;

int EventStore::upsertPrompt(const std::string& session, const std::string& toolCallId,
                              const std::string& prompt, const std::string& context) {
    if (!m_impl->db) return -1;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    const char* sql =
        "INSERT OR REPLACE INTO pending_prompts (session, tool_call_id, prompt, context, created_at) "
        "VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, session.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, toolCallId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, prompt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, context.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

std::vector<PendingPrompt> EventStore::listPending() const {
    std::vector<PendingPrompt> result;
    if (!m_impl->db) return result;

    const char* sql =
        "SELECT session, tool_call_id, prompt, context, created_at "
        "FROM pending_prompts ORDER BY created_at ASC";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingPrompt p;
        if (auto* s = sqlite3_column_text(stmt, 0)) p.session = reinterpret_cast<const char*>(s);
        if (auto* t = sqlite3_column_text(stmt, 1)) p.toolCallId = reinterpret_cast<const char*>(t);
        if (auto* pr = sqlite3_column_text(stmt, 2)) p.prompt = reinterpret_cast<const char*>(pr);
        if (auto* c = sqlite3_column_text(stmt, 3)) p.context = reinterpret_cast<const char*>(c);
        p.createdAt = sqlite3_column_int64(stmt, 4);
        result.push_back(std::move(p));
    }
    sqlite3_finalize(stmt);
    return result;
}

int EventStore::resolvePrompt(const std::string& session, const std::string& toolCallId) {
    if (!m_impl->db) return -1;
    const char* sql = "DELETE FROM pending_prompts WHERE session = ? AND tool_call_id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_impl->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, session.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, toolCallId.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int EventStore::dismissPrompt(const std::string& session, const std::string& toolCallId) {
    return resolvePrompt(session, toolCallId);
}

} // namespace a0::c2
