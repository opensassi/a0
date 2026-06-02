#pragma once

#include "persistence_store.h"
#include <memory>

namespace a0::persistence {

class SqliteStore : public PersistenceStore {
public:
    explicit SqliteStore(const std::string& dbPath);
    ~SqliteStore() override;

    int registerAgent(const BuildFingerprint& fp) override;
    int64_t createSession(const std::string& uuid, int64_t rootId, int64_t parentId, int agentId) override;
    void endSession(int64_t sessionId) override;
    int64_t appendMessage(int64_t sessionId,
                           std::optional<int64_t> subSessionId,
                           int seq,
                           const std::string& role,
                           const std::string& content,
                           const std::string& toolCallsJson,
                           const std::string& toolCallId,
                           const std::string& name,
                           const std::string& resultJson) override;
    std::vector<Message> loadMessages(int64_t sessionId,
                                       std::optional<int64_t> subSessionId = std::nullopt) override;
    int64_t findSessionByUuid(const std::string& uuid) const override;
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
    int64_t appendInvocation(int64_t messageId,
                              int skillId,
                              const std::string& toolName,
                              const std::string& paramsJson,
                              const std::string& outputJson) override;
    std::vector<InvocationRow> loadInvocations(int type,
                                                 const std::string& name) const override;

    int saveSessionContext(const SessionContextRow& row) override;
    SessionContextRow loadSessionContext(int64_t sessionId) const override;

    /// Expose the raw sqlite3 handle for ad-hoc queries.
    void* handle() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::persistence
