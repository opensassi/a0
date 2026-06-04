#include "session_manager.h"
#include "../persistence/persistence_store.h"
#include <stdexcept>

namespace a0::tui {

SessionManager::SessionManager(::a0::persistence::PersistenceStore* persistence)
    : m_persistence(persistence) {}

SessionManager::~SessionManager() = default;

int64_t SessionManager::create(const std::string& uuid, int64_t agentId) {
    if (!m_persistence) return -1;
    int64_t dbId = m_persistence->createSession(uuid, 0, 0, static_cast<int>(agentId));
    if (dbId > 0) {
        m_currentUuid = uuid;
        m_currentDbId = dbId;
    }
    return dbId;
}

std::vector<SessionInfo> SessionManager::list(int limit) const {
    std::vector<SessionInfo> result;
    if (!m_persistence) return result;
    // PersistenceStore doesn't have a listSessions method,
    // so we rely on findSessionByUuid for resume only.
    // For listing, we'd need a SQL-level query — stub returns empty.
    return result;
}

int SessionManager::resume(const std::string& uuid, int64_t& outDbId) {
    if (!m_persistence) return -1;
    int64_t dbId = m_persistence->findSessionByUuid(uuid);
    if (dbId <= 0) return -1;
    m_currentUuid = uuid;
    m_currentDbId = dbId;
    outDbId = dbId;
    return 0;
}

std::string SessionManager::currentUuid() const {
    return m_currentUuid;
}

int64_t SessionManager::currentDbId() const {
    return m_currentDbId;
}

void SessionManager::endCurrent() {
    if (m_currentDbId > 0 && m_persistence) {
        m_persistence->endSession(m_currentDbId);
    }
    m_currentUuid.clear();
    m_currentDbId = 0;
}

int SessionManager::xMessageCount(int64_t sessionDbId) const {
    if (!m_persistence) return 0;
    auto msgs = m_persistence->loadMessages(sessionDbId);
    return static_cast<int>(msgs.size());
}

} // namespace a0::tui
