#pragma once

#include <memory>
#include <string>
#include <vector>

#include "agent_interfaces.h"
#include "llm_provider.h"
#include "mpsc.h"
#include "skills/skills.h"

namespace a0 {

/// State-machine driven agent core.
///
/// Replaces both xRunForkedLoop() and processGoalStreaming() with a single
/// tick()-driven implementation. Designed for event-loop integration.
///
/// States:
///   Idle           -- waiting for a goal (submitGoal transitions to AwaitingLlm)
///   AwaitingLlm    -- LLM request in flight (tick() drives provider, emits events)
///   ExecutingTools -- tool calls received, executing them (tick() runs tools)
///
class DrivenCore {
public:
    DrivenCore(LlmProvider* provider,
               a0::skills::SkillManager* skillMgr,
               a0::persistence::PersistenceStore* persistence = nullptr);

    /// Submit a new goal. Starts an LLM request. Non-blocking -- call tick() to drive progress.
    void submitGoal(const std::string& goal);

    /// Synchronous run: submit goal and poll until idle. Returns the final output text.
    std::string runSync(const std::string& goal);

    /// Drive the state machine. Returns events for the UI layer.
    std::vector<mpsc::AppCoreEvent> tick();

    /// True when idle (no goal in progress).
    bool idle() const { return m_state == CoreState::Idle; }

    /// Set the persona name for base prompt selection.
    void setPersona(const std::string& persona) { m_personaName = persona; }

    /// Set persona skill/tool filters for schema building.
    void setPersonaSkills(const std::vector<std::string>& skills) { m_personaSkills = skills; }
    void setPersonaTools(const std::vector<std::string>& tools) { m_personaTools = tools; }

    /// Cancel the current goal.
    void cancel();

    /// Set session for persistence recording.
    void setSession(int64_t sessionDbId, const std::string& sessionUuid) {
        m_sessionDbId = sessionDbId;
        m_sessionUuid = sessionUuid;
    }

    int64_t sessionDbId() const { return m_sessionDbId; }

    /// Get the last completed goal result (populated by runSync or xFinishGoal/xFailGoal).
    const std::string& lastResult() const { return m_lastResult; }

private:
    enum class CoreState {
        Idle,
        AwaitingLlm,
        ExecutingTools
    };

    CoreState m_state = CoreState::Idle;
    LlmProvider* m_provider;
    a0::skills::SkillManager* m_skillMgr;
    a0::persistence::PersistenceStore* m_persistence;

    std::string m_lastResult;
    std::string m_personaName;
    std::vector<std::string> m_personaSkills;
    std::vector<std::string> m_personaTools;
    std::string m_sessionUuid;
    int64_t m_sessionDbId = 0;
    int64_t m_subSessionId = 0;
    int m_seq = 0;
    int m_turnCount = 0;
    bool m_systemPromptPersisted = false;

    std::vector<Message> m_messages;
    std::vector<ToolSchema> m_toolSchemas;
    std::vector<ToolSchema> m_emptySchemas;
    std::unordered_map<std::string, std::string> m_dispatch;

    std::string m_accumText;

    struct PendingToolCall {
        std::string id;
        std::string name;
        json arguments;
    };
    std::vector<PendingToolCall> m_pendingToolCalls;

    static constexpr int MAX_TURNS = 25;

    void xBuildToolSchemas();
    void xStartLlmRequest(bool includeTools = true);
    void xHandleLlmEvents(const std::vector<mpsc::AppCoreEvent>& events);
    std::vector<mpsc::AppCoreEvent> xExecuteTools();
    void xFinishGoal(const std::string& text);
    void xFailGoal(const std::string& error);
    void xPersistMessage(const std::string& role, const std::string& content,
                         const std::string& toolCallId = "",
                         const std::vector<ToolCall>& toolCalls = {});
};

} // namespace a0
