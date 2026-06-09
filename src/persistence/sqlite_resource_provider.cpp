#include "persistence/sqlite_resource_provider.h"
#include "persistence/null_resource_provider.h"

#include <sqlite3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace a0::persistence {

// ============================================================================
// Internal helper: WriterImpl
// ============================================================================

namespace {

class WriterImpl : public ResourceWriter {
public:
    WriterImpl(int64_t id, int64_t streamId, sqlite3* db,
               int64_t flushSize, const std::string& direction)
        : m_id(id)
        , m_streamId(streamId)
        , m_db(db)
        , m_flushSize(flushSize)
        , m_direction(direction)
    {}

    ~WriterImpl() override {
        if (!m_closed) close();
    }

    int64_t id() const override { return m_id; }

    void append(const std::string& data) override {
        if (m_closed) return;
        m_buffer += data;
        if (static_cast<int64_t>(m_buffer.size()) >= m_flushSize) {
            xFlush(false);
        }
    }

    void close() override {
        if (m_closed) return;
        if (!m_buffer.empty()) {
            xFlush(true);
        }
        m_closed = true;
    }

    bool closed() const override { return m_closed; }

private:
    int64_t m_id;
    int64_t m_streamId;
    sqlite3* m_db;
    int64_t m_flushSize;
    std::string m_direction;
    std::string m_buffer;
    bool m_closed = false;
    int m_seq = 0;

    void xFlush(bool isFinal) {
        // Insert chunk row
        sqlite3_stmt* stmt = nullptr;
        std::string sql = "INSERT INTO stream_chunk (stream_id, seq, direction, data, timestamp) "
                          "VALUES (?, ?, ?, ?, ?)";
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int64(stmt, 1, m_streamId);
        sqlite3_bind_int(stmt, 2, ++m_seq);
        sqlite3_bind_text(stmt, 3, m_direction.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, m_buffer.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 5, std::chrono::system_clock::now().time_since_epoch().count());
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        m_buffer.clear();

        if (isFinal) {
            sqlite3_stmt* up = nullptr;
            std::string usql = "UPDATE stream SET ended_at = ? WHERE id = ?";
            if (sqlite3_prepare_v2(m_db, usql.c_str(), -1, &up, nullptr) == SQLITE_OK) {
                sqlite3_bind_int64(up, 1, std::chrono::system_clock::now().time_since_epoch().count());
                sqlite3_bind_int64(up, 2, m_streamId);
                sqlite3_step(up);
                sqlite3_finalize(up);
            }
        }
    }
};

class HandleImpl : public ResourceHandle {
public:
    HandleImpl(int64_t id, std::string data)
        : m_id(id)
        , m_data(std::move(data))
    {}

    int64_t id() const override { return m_id; }
    bool hasMore() const override { return m_offset < static_cast<int64_t>(m_data.size()); }

    std::string readNext() override {
        if (!hasMore()) return {};
        std::string result = m_data.substr(m_offset);
        m_offset = m_data.size();
        return result;
    }

    std::string read(int64_t offset, int64_t limit) override {
        if (offset >= static_cast<int64_t>(m_data.size())) return {};
        int64_t avail = static_cast<int64_t>(m_data.size()) - offset;
        int64_t len = (limit < 0) ? avail : std::min(limit, avail);
        return m_data.substr(offset, len);
    }

    int64_t size() const override { return m_data.size(); }

private:
    int64_t m_id;
    std::string m_data;
    int64_t m_offset = 0;
};

} // anonymous namespace

// ============================================================================
// SqliteResourceProvider
// ============================================================================

class SqliteResourceProvider::Impl {
public:
    Impl(const std::string& dbPath, int64_t tokenFlush, int64_t toolFlush, int64_t previewSize)
        : m_tokenFlushSize(tokenFlush)
        , m_toolFlushSize(toolFlush)
        , m_outputPreviewSize(previewSize)
    {
        sqlite3_open(dbPath.c_str(), &m_db);
        if (m_db) {
            sqlite3_exec(m_db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
            xEnsureTables();
        }
    }

    ~Impl() {
        if (m_db) sqlite3_close(m_db);
    }

    int64_t nextId() { return ++m_nextId; }

    sqlite3* db() { return m_db; }
    int64_t tokenFlushSize() const { return m_tokenFlushSize; }
    int64_t toolFlushSize() const { return m_toolFlushSize; }
    int64_t outputPreviewSize() const { return m_outputPreviewSize; }

    void setTokenFlushSize(int64_t v) { m_tokenFlushSize = v; }
    void setToolFlushSize(int64_t v) { m_toolFlushSize = v; }
    void setOutputPreviewSize(int64_t v) { m_outputPreviewSize = v; }

private:
    sqlite3* m_db = nullptr;
    int64_t m_nextId = 0;
    int64_t m_tokenFlushSize;
    int64_t m_toolFlushSize;
    int64_t m_outputPreviewSize;
    std::mutex m_mutex;

    void xEnsureTables() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS stream ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  session_id INTEGER NOT NULL DEFAULT 0,"
            "  tool_call_id TEXT,"
            "  name TEXT NOT NULL,"
            "  context_type TEXT NOT NULL,"
            "  context_id TEXT,"
            "  cwd TEXT,"
            "  created_at INTEGER NOT NULL,"
            "  ended_at INTEGER,"
            "  exit_code INTEGER"
            ");"
            "CREATE TABLE IF NOT EXISTS stream_chunk ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  stream_id INTEGER NOT NULL REFERENCES stream(id),"
            "  seq INTEGER NOT NULL,"
            "  direction TEXT NOT NULL,"
            "  data TEXT NOT NULL,"
            "  timestamp INTEGER NOT NULL"
            ");";
        sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr);
    }
};

SqliteResourceProvider::SqliteResourceProvider(const std::string& dbPath,
                                                int64_t tokenFlushSize,
                                                int64_t toolFlushSize,
                                                int64_t outputPreviewSize)
    : m_impl(std::make_unique<Impl>(dbPath, tokenFlushSize, toolFlushSize, outputPreviewSize))
{}

SqliteResourceProvider::~SqliteResourceProvider() = default;

std::unique_ptr<ResourceWriter> SqliteResourceProvider::create(ResourceType type) {
    int64_t id = m_impl->nextId();
    std::string contextType;
    std::string direction;
    int64_t flushSize = m_impl->tokenFlushSize();

    switch (type) {
        case ResourceType::LlmStream:
            contextType = "llm";
            direction = "llm_token";
            flushSize = m_impl->tokenFlushSize();
            break;
        case ResourceType::ToolOutput:
        case ResourceType::ToolInvocation:
            contextType = "tool";
            direction = "tool_stdout";
            flushSize = m_impl->toolFlushSize();
            break;
        case ResourceType::TerminalStream:
            contextType = "terminal";
            direction = "stdout";
            flushSize = m_impl->toolFlushSize();
            break;
    }

    // Create stream row
    sqlite3* db = m_impl->db();
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "INSERT INTO stream (name, context_type, created_at, exit_code) VALUES (?, ?, ?, 0)";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::make_unique<NullResourceProvider>()->create(type);
    }
    sqlite3_bind_text(stmt, 1, contextType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, contextType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, std::chrono::system_clock::now().time_since_epoch().count());
    sqlite3_step(stmt);
    int64_t streamId = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);

    return std::make_unique<WriterImpl>(id, streamId, db, flushSize, direction);
}

std::unique_ptr<ResourceHandle> SqliteResourceProvider::open(ResourceType type, int64_t id) {
    sqlite3* db = m_impl->db();
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT data FROM stream_chunk WHERE stream_id = ? ORDER BY seq";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::make_unique<HandleImpl>(id, "");
    }
    sqlite3_bind_int64(stmt, 1, id);

    std::string allData;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (text) allData += text;
    }
    sqlite3_finalize(stmt);

    return std::make_unique<HandleImpl>(id, allData);
}

void SqliteResourceProvider::setTokenFlushSize(int64_t bytes) { m_impl->setTokenFlushSize(bytes); }
void SqliteResourceProvider::setToolFlushSize(int64_t bytes) { m_impl->setToolFlushSize(bytes); }
void SqliteResourceProvider::setOutputPreviewSize(int64_t bytes) { m_impl->setOutputPreviewSize(bytes); }

// ============================================================================
// NullResourceProvider
// ============================================================================

namespace {

class NullWriter : public ResourceWriter {
public:
    int64_t id() const override { return 0; }
    void append(const std::string&) override {}
    void close() override {}
    bool closed() const override { return true; }
};

class NullHandle : public ResourceHandle {
public:
    int64_t id() const override { return 0; }
    bool hasMore() const override { return false; }
    std::string readNext() override { return {}; }
    std::string read(int64_t, int64_t) override { return {}; }
    int64_t size() const override { return 0; }
};

} // anonymous namespace

std::unique_ptr<ResourceWriter> NullResourceProvider::create(ResourceType) {
    return std::make_unique<NullWriter>();
}

std::unique_ptr<ResourceHandle> NullResourceProvider::open(ResourceType, int64_t id) {
    return std::make_unique<NullHandle>();
}

} // namespace a0::persistence
