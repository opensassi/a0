#include "tool_state.h"

void ToolState::set(const std::string& key, const json& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state[key] = value;
}

json ToolState::get(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_state.find(key);
    if (it != m_state.end())
        return it->second;
    return json();
}

bool ToolState::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state.find(key) != m_state.end();
}

void ToolState::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.erase(key);
}

void ToolState::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.clear();
}
