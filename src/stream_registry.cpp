#include "stream_registry.h"
#include <algorithm>

namespace a0 {

int64_t StreamRegistry::registerStream(
    int64_t streamId,
    int pid,
    std::function<void(const std::string&)> sendInput,
    std::function<int()> wait,
    std::function<void()> cancel)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_streams[streamId] = StreamEntry{
        streamId, pid, std::move(sendInput), std::move(wait), std::move(cancel)
    };
    return streamId;
}

void StreamRegistry::unregisterStream(int64_t streamId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_streams.erase(streamId);
}

StreamRegistry::StreamEntry* StreamRegistry::getStream(int64_t streamId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_streams.find(streamId);
    if (it != m_streams.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<int64_t> StreamRegistry::listActiveStreams() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<int64_t> ids;
    ids.reserve(m_streams.size());
    for (const auto& pair : m_streams) {
        ids.push_back(pair.first);
    }
    return ids;
}

void StreamRegistry::cancelAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_streams) {
        if (pair.second.cancel) {
            pair.second.cancel();
        }
    }
    m_streams.clear();
}

} // namespace a0