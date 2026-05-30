#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <functional>
#include "command_runner.h"

namespace a0 {

/// Tracks all active streaming processes (tool invocations, terminals).
/// Thread-safe. Used by AgentCore, ToolRunner, and SkillRunner.
class StreamRegistry {
public:
    /// Register a stream. Returns the streamId that was assigned.
    /// sendInput is a function that writes to the stream's stdin.
    /// wait blocks until the stream completes.
    /// cancel kills the stream.
    int64_t registerStream(
        int64_t streamId,
        int pid,
        std::function<void(const std::string&)> sendInput,
        std::function<int()> wait,
        std::function<void()> cancel);

    /// Remove a stream from tracking (after it ends or errors).
    void unregisterStream(int64_t streamId);

    /// Get a stream handle for sending input.
    /// Returns nullptr if streamId is not tracked.
    struct StreamEntry {
        int64_t streamId;
        int pid;
        std::function<void(const std::string&)> sendInput;
        std::function<int()> wait;
        std::function<void()> cancel;
    };

    StreamEntry* getStream(int64_t streamId);

    /// List all active stream IDs.
    std::vector<int64_t> listActiveStreams() const;

    /// Cancel all active streams (e.g., on agent shutdown).
    void cancelAll();

private:
    mutable std::mutex m_mutex;
    std::unordered_map<int64_t, StreamEntry> m_streams;
};

} // namespace a0