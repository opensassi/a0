#pragma once

#include "persistence_store.h"
#include <memory>

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
