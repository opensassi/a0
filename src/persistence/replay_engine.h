#pragma once

#include "persistence_store.h"
#include <string>
#include <vector>

namespace a0::persistence {

/// Reads a stored session and replays it against the current binary.
/// LLM responses are injected from the log.
/// Tools are re-executed and compared against stored results.
class ReplayEngine {
public:
    explicit ReplayEngine(PersistenceStore* store);

    /// Replay an entire session.
    /// \param sessionId   Session to replay.
    /// \param divergence  Populated with details if replay fails.
    /// \retval 0  All messages match.
    /// \retval 1  Divergence found.
    /// \retval -1 Session not found.
    int replay(int64_t sessionId, std::string& divergence);

    /// Replay up to a specific message id.
    int replayTo(int64_t sessionId, int64_t upToMessageId, std::string& divergence);

private:
    PersistenceStore* m_store;

    int xStep(const Message& msg, std::string& divergence);
    int xCompareToolResult(const Message& msg,
                           const std::string& actualResult,
                           std::string& divergence);
};

} // namespace a0::persistence
