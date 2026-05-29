#include "replay_engine.h"
#include "../command_runner.h"
#include <sstream>

namespace a0::persistence {

using a0::CommandRunner;

ReplayEngine::ReplayEngine(PersistenceStore* store)
    : m_store(store)
{
}

int ReplayEngine::replay(int64_t sessionId, std::string& divergence)
{
    auto messages = m_store->loadMessages(sessionId);
    if (messages.empty()) {
        divergence = "session not found or empty";
        return -1;
    }

    for (const auto& msg : messages) {
        int ret = xStep(msg, divergence);
        if (ret != 0) {
            std::ostringstream ss;
            ss << "divergence at message " << msg.id
               << " (role=" << msg.role << "): " << divergence;
            divergence = ss.str();
            return ret;
        }
    }
    return 0;
}

int ReplayEngine::replayTo(int64_t sessionId, int64_t upToMessageId,
                            std::string& divergence)
{
    auto messages = m_store->loadMessages(sessionId);
    if (messages.empty()) {
        divergence = "session not found or empty";
        return -1;
    }

    for (const auto& msg : messages) {
        if (msg.id > upToMessageId) break;
        int ret = xStep(msg, divergence);
        if (ret != 0) {
            std::ostringstream ss;
            ss << "divergence at message " << msg.id
               << " (role=" << msg.role << "): " << divergence;
            divergence = ss.str();
            return ret;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int ReplayEngine::xStep(const Message& msg, std::string& divergence)
{
    if (msg.role == "user" || msg.role == "system") {
        // These are inputs — no comparison needed
        return 0;
    }

    if (msg.role == "assistant") {
        // LLM response was injected — verify tool calls match
        // For full replay this is handled by comparing tool execution results.
        // Content-only assistant messages have no tool calls to verify.
        return 0;
    }

    if (msg.role == "tool") {
        // Re-execute the tool and compare
        std::string actualResult;
        int rc = xCompareToolResult(msg, actualResult, divergence);
        if (rc != 0) {
            return rc;
        }
        return 0;
    }

    return 0;
}

int ReplayEngine::xCompareToolResult(const Message& msg,
                                      const std::string& actualResult,
                                      std::string& divergence)
{
    (void)actualResult;

    // Build command from the stored tool name
    std::string cmd = msg.name;
    if (cmd.empty()) {
        divergence = "tool message has no name";
        return -1;
    }

    // Re-execute the command
    // Note: in a full implementation, we'd use the stored params
    // to reconstruct the exact tool invocation.
    auto result = CommandRunner::run(cmd, "", 30);

    if (result.timedOut) {
        divergence = "tool re-execution timed out";
        return 1;
    }
    if (result.exitCode != 0) {
        divergence = "tool re-execution failed: " + result.stderr;
        return 1;
    }

    // Compare against stored result
    if (result.stdout != msg.resultJson) {
        divergence = "output mismatch\n  expected: " + msg.resultJson +
                     "\n  actual:   " + result.stdout;
        return 1;
    }

    return 0;
}

} // namespace a0::persistence
