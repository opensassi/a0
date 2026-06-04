#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <optional>
#include <ctime>
#include "../../../src/persistence/persistence_store.h"

namespace a0::persistence {

class MockPersistenceStore : public PersistenceStore {
public:
    struct MockSession {
        std::string uuid;
        int64_t dbId;
        int64_t rootId;
        int64_t parentId;
        int agentId;
        bool ended = false;
        std::vector<Message> messages;
    };

    std::unordered_map<int64_t, MockSession> sessions;
    std::unordered_map<std::string, int64_t> uuidMap;
    int64_t nextSessionId = 100;
    int64_t nextMessageId = 1000;
    int64_t nextStreamId = 200;

    int registerAgent(const BuildFingerprint&) override { return 1; }

    int64_t createSession(const std::string& uuid, int64_t rootId, int64_t parentId, int agentId) override {
        int64_t id = nextSessionId++;
        MockSession session;
        session.uuid = uuid;
        session.dbId = id;
        session.rootId = rootId;
        session.parentId = parentId;
        session.agentId = agentId;
        sessions[id] = session;
        uuidMap[uuid] = id;
        return id;
    }

    void endSession(int64_t sessionId) override {
        auto it = sessions.find(sessionId);
        if (it != sessions.end()) {
            it->second.ended = true;
        }
    }

    int64_t appendMessage(int64_t sessionId,
                           std::optional<int64_t> subSessionId,
                           int seq,
                           const std::string& role,
                           const std::string& content,
                           const std::string& toolCallsJson,
                           const std::string& toolCallId,
                           const std::string& name,
                           const std::string& resultJson) override {
        Message msg;
        msg.id = nextMessageId++;
        msg.sessionId = sessionId;
        msg.role = role;
        msg.content = content;
        msg.toolCallsJson = toolCallsJson;
        msg.toolCallId = toolCallId;
        msg.name = name;
        msg.resultJson = resultJson;
        msg.createdAt = std::time(nullptr);
        msg.subSessionId = subSessionId.value_or(0);
        msg.seq = seq;
        auto it = sessions.find(sessionId);
        if (it != sessions.end()) {
            it->second.messages.push_back(msg);
        }
        return msg.id;
    }

    std::vector<Message> loadMessages(int64_t sessionId,
                                       std::optional<int64_t> subSessionId) override {
        auto it = sessions.find(sessionId);
        if (it == sessions.end()) return {};
        if (subSessionId.has_value()) {
            std::vector<Message> filtered;
            for (const auto& m : it->second.messages) {
                if (m.subSessionId == subSessionId.value()) {
                    filtered.push_back(m);
                }
            }
            return filtered;
        }
        return it->second.messages;
    }

    int64_t findSessionByUuid(const std::string& uuid) const override {
        auto it = uuidMap.find(uuid);
        if (it != uuidMap.end()) return it->second;
        return 0;
    }

    void flush() override {}

    int64_t createStream(int64_t, const std::string&, const std::string&,
                          const std::string&, const std::string&,
                          const std::string&, const std::string&) override {
        return nextStreamId++;
    }

    int appendChunk(int64_t, int, const std::string&, const std::string&) override { return 0; }

    int endStream(int64_t, int) override { return 0; }

    std::vector<StreamChunk> loadStreamChunks(int64_t, int, int) override { return {}; }

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
};

} // namespace a0::persistence
