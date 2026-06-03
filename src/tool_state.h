#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

/// Per-session key-value state bag for tools that need to share state
/// across invocations (e.g., a browser page handle, database connection).
/// Thread-safe.
class ToolState {
public:
    void set(const std::string& key, const json& value);
    json get(const std::string& key) const;
    bool has(const std::string& key) const;
    void remove(const std::string& key);
    void clear();

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, json> m_state;
};
