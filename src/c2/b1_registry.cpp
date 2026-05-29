#include "b1_registry.h"
#include <algorithm>

namespace a0::c2 {

int B1Registry::upsertB1(int pid, const std::string& workdir, const std::string& hostname) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_b1s.find(pid);
    if (it != m_b1s.end()) {
        it->second.workdir = workdir;
        it->second.hostname = hostname;
        it->second.lastUpdate = std::chrono::steady_clock::now();
        return 0;
    }

    B1Instance inst;
    inst.pid = pid;
    inst.workdir = workdir;
    inst.hostname = hostname;
    inst.connectedAt = std::chrono::steady_clock::now();
    inst.lastUpdate = inst.connectedAt;
    m_b1s[pid] = std::move(inst);
    return 0;
}

int B1Registry::removeB1(int pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_b1s.find(pid);
    if (it == m_b1s.end()) {
        return -1;
    }
    m_b1s.erase(it);
    return 0;
}

int B1Registry::updateAgents(int pid, const std::vector<AgentSummary>& agents) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_b1s.find(pid);
    if (it == m_b1s.end()) {
        return -1;
    }
    it->second.agents = agents;
    it->second.lastUpdate = std::chrono::steady_clock::now();
    return 0;
}

std::vector<B1Instance> B1Registry::listB1s() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<B1Instance> result;
    result.reserve(m_b1s.size());
    for (const auto& pair : m_b1s) {
        result.push_back(pair.second);
    }
    return result;
}

void B1Registry::getStats(int& totalB1s, int& totalAgents, int& crashedCount) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    totalB1s = static_cast<int>(m_b1s.size());
    totalAgents = 0;
    crashedCount = 0;
    for (const auto& pair : m_b1s) {
        totalAgents += static_cast<int>(pair.second.agents.size());
        for (const auto& agent : pair.second.agents) {
            if (agent.state == "crashed") {
                ++crashedCount;
            }
        }
    }
}

int B1Registry::pruneStale(int maxAgeSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    int removed = 0;
    auto it = m_b1s.begin();
    while (it != m_b1s.end()) {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.lastUpdate).count();
        if (age > maxAgeSeconds) {
            it = m_b1s.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

} // namespace a0::c2
