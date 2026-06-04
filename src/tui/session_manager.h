#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "../persistence/persistence_store.h"

namespace a0::tui {

struct SessionInfo {
    std::string uuid;
    int64_t dbId;
    std::string startedAt;
    int messageCount;
};

class SessionManager {
public:
    explicit SessionManager(::a0::persistence::PersistenceStore* persistence);
    virtual ~SessionManager();

    int64_t create(const std::string& uuid, int64_t agentId = 0);
    std::vector<SessionInfo> list(int limit = 20) const;
    int resume(const std::string& uuid, int64_t& outDbId);

    std::string currentUuid() const;
    int64_t currentDbId() const;

    void endCurrent();

private:
    ::a0::persistence::PersistenceStore* m_persistence;
    std::string m_currentUuid;
    int64_t m_currentDbId = 0;

    int xMessageCount(int64_t sessionDbId) const;
};

} // namespace a0::tui
