#pragma once

#include <string>
#include <vector>
#include <memory>

namespace a0::c2 {

struct PendingPrompt {
    std::string session;
    std::string toolCallId;
    std::string prompt;
    std::string context;
    int64_t createdAt = 0;
};

class EventStore {
public:
    explicit EventStore(const std::string& dbPath);
    ~EventStore();

    int upsertPrompt(const std::string& session, const std::string& toolCallId,
                     const std::string& prompt, const std::string& context);
    std::vector<PendingPrompt> listPending() const;
    int resolvePrompt(const std::string& session, const std::string& toolCallId);
    int dismissPrompt(const std::string& session, const std::string& toolCallId);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace a0::c2
