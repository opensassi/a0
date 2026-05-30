#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>

namespace a0::c2 {

class SseManager {
public:
    SseManager() = default;
    ~SseManager() = default;

    int addClient(std::function<void(const std::string&)> sendFn);
    void removeClient(int id);
    int broadcast(const std::string& eventType, const std::string& dataJson);
    int broadcast(const std::string& eventType, const std::string& dataJson, const std::string& id);
    size_t clientCount() const;

private:
    struct Client {
        int id;
        std::function<void(const std::string&)> send;
    };
    mutable std::mutex m_mutex;
    std::unordered_map<int, Client> m_clients;
    int m_nextId = 1;
};

} // namespace a0::c2
