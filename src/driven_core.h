#pragma once

#include <memory>
#include <string>
#include <vector>

#include "agent_interfaces.h"
#include "driven_provider.h"
#include "mpsc.h"
#include "skills/skills.h"

namespace a0 {

/// State-machine driven agent core.
///
/// Replaces both xRunForkedLoop() and processGoalStreaming() with a single
/// tick()-driven implementation. Designed for event-loop integration.
///
/// States:
///   Idle           — waiting for a goal (submitGoal transitions to AwaitingLlm)
///   AwaitingLlm    — LLM request in flight (tick() drives provider, emits events)
///   ExecutingTools — tool calls received, executing them (tick() runs tools)
///
class DrivenCore {
public:
    DrivenCore(DrivenProvider* provider,
               a0::skills::SkillManager* skillMgr,
               a0::persistence::PersistenceStore* persistence = nullptr);

    /// Submit a new goal. Starts an LLM request. Non-blocking — call tick() to drive progress.
    void submitGoal(const std::string& goal);

    /// Drive the state machine. Returns events for the UI layer.
    /// Call this from the event loop on every iteration.
    std::vector<mpsc::AppCoreEvent> tick();

    /// True when idle (no goal in progress).
    bool idle() const { return m_state == CoreState::Idle; }

    /// Cancel the current goal.
    void cancel();

    /// Set session for persistence recording.
    void setSession(int64_t sessionDbId, const std::string& sessionUuid) {
        m_sessionDbId = sessionDbId;
        m_sessionUuid = sessionUuid;
    }

    int64_t sessionDbId() const { return m_sessionDbId; }

private:
    enum class CoreState {
        Idle,
        AwaitingLlm,
        ExecutingTools
    };

    CoreState m_state = CoreState::Idle;
    DrivenProvider* m_provider;
    a0::skills::SkillManager* m_skillMgr;
    a0::persistence::PersistenceStore* m_persistence;

    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    int64_t m_subSessionId = 0;
    int m_seq = 0;
    int m_turnCount = 0;

    // Conversation history for the current goal
    std::vector<Message> m_messages;

    // Tools for the current goal
    std::vector<ToolSchema> m_toolSchemas;
    std::vector<ToolSchema> m_emptySchemas;  // always empty, used when includeTools=false
    std::unordered_map<std::string, std::string> m_dispatch;

    // Accumulated LLM response (streaming text)
    std::string m_accumText;

    // Tool call state
    struct PendingToolCall {
        std::string id;
        std::string name;
        json arguments;
    };
    std::vector<PendingToolCall> m_pendingToolCalls;

    static constexpr int MAX_TURNS = 25;

    void xBuildInitialMessages(const std::string& goal);
    void xBuildToolSchemas();
    void xStartLlmRequest(bool includeTools = true);
    void xHandleLlmEvents(const std::vector<mpsc::AppCoreEvent>& events);
    void xExecuteTools();
    void xFinishGoal(const std::string& text);
    void xFailGoal(const std::string& error);
    void xPersistMessage(const std::string& role, const std::string& content,
                         const std::string& toolCallId = "",
                         const std::vector<ToolCall>& toolCalls = {});
};

} // namespace a0
