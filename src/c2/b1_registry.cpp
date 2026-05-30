#include "b1_registry.h"
#include "sse_manager.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <sstream>

namespace a0::c2 {

static std::string xTimestamp() {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(now);
}

int B1Registry::upsertB1(int pid, const std::string& workdir, const std::string& hostname) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_b1s.find(pid);
        if (it != m_b1s.end()) {
            it->second.workdir = workdir;
            it->second.hostname = hostname;
            it->second.lastUpdate = std::chrono::steady_clock::now();
        } else {
            B1Instance inst;
            inst.pid = pid;
            inst.workdir = workdir;
            inst.hostname = hostname;
            inst.connectedAt = std::chrono::steady_clock::now();
            inst.lastUpdate = inst.connectedAt;
            m_b1s[pid] = std::move(inst);
        }
    }

    if (m_sse) {
        nlohmann::json j;
        j["pid"] = pid;
        j["workdir"] = workdir;
        j["hostname"] = hostname;
        j["timestamp"] = xTimestamp();
        m_sse->broadcast("b1_connected", j.dump());
    }
    return 0;
}

int B1Registry::removeB1(int pid) {
    std::string workdir;
    std::string hostname;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_b1s.find(pid);
        if (it == m_b1s.end()) return -1;
        workdir = it->second.workdir;
        hostname = it->second.hostname;
        m_b1s.erase(it);
    }

    if (m_sse) {
        nlohmann::json j;
        j["pid"] = pid;
        j["workdir"] = workdir;
        j["hostname"] = hostname;
        j["timestamp"] = xTimestamp();
        m_sse->broadcast("b1_disconnected", j.dump());
    }
    return 0;
}

int B1Registry::updateAgents(int pid, const std::vector<AgentSummary>& agents) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_b1s.find(pid);
        if (it == m_b1s.end()) return -1;
        it->second.agents = agents;
        it->second.lastUpdate = std::chrono::steady_clock::now();
    }

    if (m_sse) {
        nlohmann::json j;
        j["pid"] = pid;
        j["timestamp"] = xTimestamp();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& a : agents) {
            nlohmann::json ag;
            ag["pid"] = a.pid;
            ag["session"] = a.sessionUuid;
            ag["state"] = a.state;
            arr.push_back(ag);
        }
        j["agents"] = arr;
        m_sse->broadcast("agent_update", j.dump());
    }
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
            if (agent.state == "crashed") ++crashedCount;
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
            if (m_sse) {
                nlohmann::json j;
                j["pid"] = it->second.pid;
                j["workdir"] = it->second.workdir;
                j["hostname"] = it->second.hostname;
                j["timestamp"] = xTimestamp();
                j["reason"] = "stale";
                m_sse->broadcast("b1_disconnected", j.dump());
            }
            it = m_b1s.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

} // namespace a0::c2
