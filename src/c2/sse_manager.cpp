#include "sse_manager.h"
#include "trace.h"
#include <sstream>

namespace a0::c2 {

int SseManager::addClient(std::function<void(const std::string&)> sendFn) {
    std::lock_guard<std::mutex> lock(m_mutex);
    int id = m_nextId++;
    m_clients[id] = {id, std::move(sendFn)};
    return id;
}

void SseManager::removeClient(int id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clients.erase(id);
}

int SseManager::broadcast(const std::string& eventType, const std::string& dataJson) {
    return broadcast(eventType, dataJson, "");
}

int SseManager::broadcast(const std::string& eventType, const std::string& dataJson, const std::string& id) {
    std::ostringstream oss;
    if (!id.empty()) {
        oss << "id: " << id << "\n";
    }
    oss << "event: " << eventType << "\n";
    oss << "data: " << dataJson << "\n\n";
    std::string payload = oss.str();

    TRACE_LOG("c2: sse broadcast event=" << eventType << " clients=" << m_clients.size());
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = 0;
    auto it = m_clients.begin();
    while (it != m_clients.end()) {
        try {
            it->second.send(payload);
            ++count;
            ++it;
        } catch (...) {
            it = m_clients.erase(it);
        }
    }
    return count;
}

size_t SseManager::clientCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clients.size();
}

} // namespace a0::c2
