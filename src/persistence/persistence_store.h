#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

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
    int64_t subSessionId = 0;
    int seq = 0;
};

struct Stream {
    int64_t id;
    int64_t sessionId;
    std::string toolCallId;
    std::string terminalId;
    std::string name;
    std::string contextType;
    std::string contextId;
    std::string cwd;
    int64_t createdAt;
    int64_t endedAt;
    int exitCode;
};

struct StreamChunk {
    int64_t id;
    int64_t streamId;
    int seq;
    std::string direction;
    std::string data;
    int64_t timestamp;
};

struct SessionContextRow {
    int64_t sessionId;
    std::string sessionUuid;
    std::string originalCwd;
    std::string worktreePath;
    std::string gitRepoRoot;
    std::string gitBranch;
    std::string gitCommit;
};

struct InvocationRow {
    int64_t id;
    int64_t messageId;
    int64_t skillId;
    std::string toolName;
    std::string paramsJson;
    std::string outputJson;
    int64_t timestamp;
};

class PersistenceStore {
public:
    virtual ~PersistenceStore() = default;

    virtual int registerAgent(const BuildFingerprint& fp) = 0;

    virtual int64_t createSession(const std::string& uuid,
                                   int64_t rootId,
                                   int64_t parentId,
                                   int agentId) = 0;

    virtual void endSession(int64_t sessionId) = 0;

    virtual int64_t appendMessage(int64_t sessionId,
                                   std::optional<int64_t> subSessionId,
                                   int seq,
                                   const std::string& role,
                                   const std::string& content,
                                   const std::string& toolCallsJson,
                                   const std::string& toolCallId,
                                   const std::string& name,
                                   const std::string& resultJson) = 0;

    virtual std::vector<Message> loadMessages(int64_t sessionId,
                                               std::optional<int64_t> subSessionId = std::nullopt) = 0;

    virtual int64_t findSessionByUuid(const std::string& uuid) const = 0;

    struct SessionRow {
        int64_t id = 0;
        std::string uuid;
        int64_t startedAt = 0;
        int messageCount = 0;
    };

    virtual std::vector<SessionRow> loadSessions(int limit = 20) const = 0;

    virtual void flush() = 0;

    // --- Streaming ---

    virtual int64_t createStream(int64_t sessionId,
                                   const std::string& toolCallId,
                                   const std::string& name,
                                   const std::string& contextType,
                                   const std::string& contextId,
                                   const std::string& cwd,
                                   const std::string& terminalId = "") = 0;

    virtual int appendChunk(int64_t streamId, int seq,
                             const std::string& direction,
                             const std::string& data) = 0;

    virtual int endStream(int64_t streamId, int exitCode) = 0;

    virtual std::vector<StreamChunk> loadStreamChunks(int64_t streamId,
                                                       int offset = 0,
                                                       int limit = -1) = 0;

    virtual std::vector<Stream> listSessionStreams(int64_t sessionId) = 0;

    // --- Skill (type reference) ---

    virtual int ensureSkill(int type, const std::string& name) = 0;

    virtual int64_t appendInvocation(int64_t messageId,
                                      int skillId,
                                      const std::string& toolName,
                                      const std::string& paramsJson,
                                      const std::string& outputJson) = 0;

    virtual std::vector<InvocationRow> loadInvocations(int type,
                                                         const std::string& name) const = 0;

    // --- Session context ---

    virtual int saveSessionContext(const SessionContextRow& row) = 0;

    virtual SessionContextRow loadSessionContext(int64_t sessionId) const = 0;
};

class NullStore : public PersistenceStore {
public:
    int registerAgent(const BuildFingerprint&) override { return 1; }
    int64_t createSession(const std::string&, int64_t rootId, int64_t, int) override {
        static int64_t nextId = 100;
        if (m_nextRoot) { nextId = m_nextRoot; m_nextRoot = 0; }
        return nextId++;
    }
    void endSession(int64_t) override {}
    int64_t appendMessage(int64_t, std::optional<int64_t>, int,
                           const std::string&, const std::string&,
                           const std::string&, const std::string&,
                           const std::string&, const std::string&) override {
        static int64_t nextMsg = 1000;
        return nextMsg++;
    }
    std::vector<Message> loadMessages(int64_t, std::optional<int64_t>) override { return {}; }
    int64_t findSessionByUuid(const std::string&) const override { return 0; }
    std::vector<SessionRow> loadSessions(int) const override { return {}; }
    void flush() override {}

    int64_t createStream(int64_t, const std::string&, const std::string&,
                          const std::string&, const std::string&,
                          const std::string&, const std::string& = "") override {
        static int64_t nextStream = 200;
        return nextStream++;
    }
    int appendChunk(int64_t, int, const std::string&,
                     const std::string&) override { return 0; }
    int endStream(int64_t, int) override { return 0; }
    std::vector<StreamChunk> loadStreamChunks(int64_t, int, int) override {
        return {};
    }
    std::vector<Stream> listSessionStreams(int64_t) override { return {}; }

    int ensureSkill(int, const std::string&) override { return 1; }
    int64_t appendInvocation(int64_t, int, const std::string&,
                              const std::string&, const std::string&) override {
        static int64_t nextInv = 500;
        return nextInv++;
    }
    std::vector<InvocationRow> loadInvocations(int, const std::string&) const override {
        return {};
    }

    int saveSessionContext(const SessionContextRow&) override { return 0; }
    SessionContextRow loadSessionContext(int64_t) const override { return {}; }

private:
    int64_t m_nextRoot = 0;
};

} // namespace a0::persistence
