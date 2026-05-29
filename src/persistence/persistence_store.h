#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace a0::persistence {

struct BuildFingerprint {
    std::string binarySha1;
    std::string repoUrl;
    std::string commitHash;
    std::string dirtyHash;
};

struct Message {
    int64_t id;
    int64_t sessionId;
    std::string role;
    std::string content;
    std::string toolCallsJson;
    std::string toolCallId;
    std::string name;
    std::string resultJson;
    int64_t createdAt;
};

class PersistenceStore {
public:
    virtual ~PersistenceStore() = default;

    /// Register or look up an agent binary. Returns agent.id.
    virtual int registerAgent(const BuildFingerprint& fp) = 0;

    /// Create a new session. Returns session.id.
    /// rootId is the top-level session.id (same as return for root).
    /// parentId is 0 for root sessions.
    virtual int64_t createSession(int64_t rootId,
                                   int64_t parentId,
                                   int agentId) = 0;

    /// Mark a session as ended.
    virtual void endSession(int64_t sessionId) = 0;

    /// Append a message to the session log. Returns message.id.
    virtual int64_t appendMessage(int64_t sessionId,
                                   const std::string& role,
                                   const std::string& content,
                                   const std::string& toolCallsJson,
                                   const std::string& toolCallId,
                                   const std::string& name,
                                   const std::string& resultJson) = 0;

    /// Load all messages for a session, ordered by id.
    virtual std::vector<Message> loadMessages(int64_t sessionId) = 0;

    /// Look up session id by uuid string.
    virtual int64_t findSessionByUuid(const std::string& uuid) const = 0;

    /// Flush pending writes to disk.
    virtual void flush() = 0;
};

/// No-op implementation for testing.
class NullStore : public PersistenceStore {
public:
    int registerAgent(const BuildFingerprint&) override { return 1; }
    int64_t createSession(int64_t rootId, int64_t, int) override {
        static int64_t nextId = 100;
        if (m_nextRoot) { nextId = m_nextRoot; m_nextRoot = 0; }
        return nextId++;
    }
    void endSession(int64_t) override {}
    int64_t appendMessage(int64_t, const std::string&, const std::string&,
                           const std::string&, const std::string&,
                           const std::string&, const std::string&) override {
        static int64_t nextMsg = 1000;
        return nextMsg++;
    }
    std::vector<Message> loadMessages(int64_t) override { return {}; }
    int64_t findSessionByUuid(const std::string&) const override { return 0; }
    void flush() override {}

private:
    int64_t m_nextRoot = 0;
};

} // namespace a0::persistence
