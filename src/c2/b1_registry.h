#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>

namespace a0::c2 {

struct AgentSummary {
    int pid = 0;
    std::string sessionUuid;
    std::string state;
    int64_t connectedAt = 0;
    int64_t lastHeartbeat = 0;
};

struct B1Instance {
    int pid = 0;
    std::string workdir;
    std::string hostname;
    std::chrono::steady_clock::time_point connectedAt;
    std::chrono::steady_clock::time_point lastUpdate;
    std::vector<AgentSummary> agents;
};

class B1Registry {
public:
    B1Registry() = default;

    int upsertB1(int pid, const std::string& workdir, const std::string& hostname);
    int removeB1(int pid);
    int updateAgents(int pid, const std::vector<AgentSummary>& agents);
    std::vector<B1Instance> listB1s() const;
    void getStats(int& totalB1s, int& totalAgents, int& crashedCount) const;
    int pruneStale(int maxAgeSeconds = 60);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<int, B1Instance> m_b1s;
};

} // namespace a0::c2
