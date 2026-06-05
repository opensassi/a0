#include "driven_core.h"
#include "dependency_graph.h"
#include "persistence/persistence_store.h"
#include "trace.h"

#include <algorithm>
#include <sstream>

// ---------------------------------------------------------------------------
// UTF-8 sanitization (duplicated from agent_core.cpp until cleanup phase)
// ---------------------------------------------------------------------------

static std::string xSanitizeUtf8(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c <= 0x7F) {
            out += c;
        } else if (c >= 0xC0 && c <= 0xDF) {
            if (i + 1 < raw.size() &&
                (static_cast<unsigned char>(raw[i+1]) & 0xC0) == 0x80) {
                out += raw[i]; out += raw[i+1]; ++i;
            } else {
                out += '?';
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < raw.size() &&
                (static_cast<unsigned char>(raw[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(raw[i+2]) & 0xC0) == 0x80) {
                out += raw[i]; out += raw[i+1]; out += raw[i+2]; i += 2;
            } else {
                out += '?';
            }
        } else if (c >= 0xF0 && c <= 0xF7) {
            if (i + 3 < raw.size() &&
                (static_cast<unsigned char>(raw[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(raw[i+2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(raw[i+3]) & 0xC0) == 0x80) {
                out += raw[i]; out += raw[i+1]; out += raw[i+2]; out += raw[i+3]; i += 3;
            } else {
                out += '?';
            }
        } else {
            out += '?';
        }
    }
    return out;
}

static std::string xTruncateForLLM(const std::string& output, size_t maxBytes = 8192) {
    if (output.size() <= maxBytes) return output;
    std::string truncated = output.substr(0, maxBytes);
    truncated += "\n... (output truncated at " + std::to_string(maxBytes) + " bytes)";
    return truncated;
}

namespace a0 {

DrivenCore::DrivenCore(DrivenProvider* provider,
                       a0::skills::SkillManager* skillMgr,
                       a0::persistence::PersistenceStore* persistence)
    : m_provider(provider)
    , m_skillMgr(skillMgr)
    , m_persistence(persistence)
{}

void DrivenCore::submitGoal(const std::string& goal) {
    if (m_state != CoreState::Idle) return;

    TRACE_LOG("DrivenCore::submitGoal(" << goal << ")");

    m_turnCount = 0;
    m_accumText.clear();
    m_pendingToolCalls.clear();
    m_subSessionId = 0;
    m_seq = 0;

    xBuildInitialMessages(goal);
    xBuildToolSchemas();

    // Start streaming LLM request
    if (m_persistence && m_sessionDbId > 0) {
        m_persistence->appendMessage(m_sessionDbId, std::nullopt, m_seq++,
            "user", goal, "", "", "", "");
    }

    xStartLlmRequest(false);  // First request: no tools (matches old streaming path behavior)
    m_state = CoreState::AwaitingLlm;
}

void DrivenCore::xBuildInitialMessages(const std::string& goal) {
    m_messages.clear();
    m_messages.push_back({"user", goal});

    // Auto-analyze: call tools_for_prompt to determine which tools to expose
    if (m_skillMgr) {
        auto result = m_skillMgr->executeToolWithMeta(
            "system-meta-tools_for_prompt", {{"prompt", goal}});
        if (!result.output.empty()) {
            m_messages.push_back({"assistant", result.output});
        }
    }
}

void DrivenCore::xBuildToolSchemas() {
    m_toolSchemas.clear();
    m_dispatch.clear();

    if (!m_skillMgr) return;

    // Build dispatch table
    m_dispatch = m_skillMgr->buildDispatchTable();

    // Start with default (always-available) tools
    m_toolSchemas = m_skillMgr->schemas(true);

    // Add tools that were recommended by tools_for_prompt
    // (In the current code, m_accumulatedTools is populated by xRunForkedLoop
    //  based on the analysis output. We've already called tools_for_prompt
    //  in xBuildInitialMessages. The recommended tools are available via
    //  the dispatch table.)
    for (const auto& [shortName, qualified] : m_dispatch) {
        bool alreadyIn = false;
        for (const auto& ts : m_toolSchemas) {
            if (ts.name == shortName) { alreadyIn = true; break; }
        }
        if (alreadyIn) continue;

        // Get tool description for schema
        a0::skills::SkillTool st;
        if (m_skillMgr->getTool(qualified, st) == 0) {
            ToolSchema ts;
            ts.name = shortName;
            ts.description = st.description;
            ts.inputSchema = st.parameters;
            if (ts.inputSchema.is_null()) {
                ts.inputSchema = {{"type", "object"},
                                   {"properties", nlohmann::json::object()},
                                   {"required", nlohmann::json::array()}};
            }
            m_toolSchemas.push_back(ts);
        } else {
            // Check if it's a prompt
            Prompt sp;
            if (m_skillMgr->getPrompt(qualified, sp) == 0) {
                ToolSchema ts;
                ts.name = shortName;
                ts.description = sp.description;
                ts.inputSchema = {{"type", "object"},
                                   {"properties", nlohmann::json::object()},
                                   {"required", nlohmann::json::array()}};
                m_toolSchemas.push_back(ts);
            }
        }
    }
}

void DrivenCore::xStartLlmRequest(bool includeTools) {
    std::string systemPrompt;
    if (m_skillMgr) {
        Prompt baseP;
        if (m_skillMgr->getPrompt("system-base", baseP) == 0) {
            systemPrompt = baseP.prompt;
        }
    }

    auto& tools = includeTools ? m_toolSchemas : m_emptySchemas;
    m_provider->startRequestStreaming(systemPrompt, m_messages, tools);
    m_state = CoreState::AwaitingLlm;
}

std::vector<mpsc::AppCoreEvent> DrivenCore::tick() {
    if (m_state == CoreState::Idle) return {};
    TRACE_LOG("DrivenCore::tick state=" << (int)m_state);

    if (m_state == CoreState::AwaitingLlm) {
        auto events = m_provider->tick();
        xHandleLlmEvents(events);
        return events;
    }

    if (m_state == CoreState::ExecutingTools) {
        xExecuteTools();
        return {}; // Events are emitted by xExecuteTools via xFinishGoal
    }

    return {};
}

void DrivenCore::xHandleLlmEvents(const std::vector<mpsc::AppCoreEvent>& events) {
    for (const auto& ev : events) {
        if (std::holds_alternative<mpsc::LlmToken>(ev)) {
            m_accumText += std::get<mpsc::LlmToken>(ev).text;
        } else if (std::holds_alternative<mpsc::ToolStart>(ev)) {
            const auto& ts = std::get<mpsc::ToolStart>(ev);
            PendingToolCall ptc;
            ptc.id = "";
            ptc.name = ts.toolName;
            try {
                ptc.arguments = nlohmann::json::parse(ts.arguments);
            } catch (...) {
                ptc.arguments = ts.arguments;
            }
            m_pendingToolCalls.push_back(std::move(ptc));
        } else if (std::holds_alternative<mpsc::Complete>(ev)) {
            if (!m_pendingToolCalls.empty()) {
                // LLM returned tool_calls — execute them
                m_state = CoreState::ExecutingTools;

                nlohmann::json tcsJson = nlohmann::json::array();
                for (const auto& ptc : m_pendingToolCalls) {
                    nlohmann::json j;
                    j["id"] = ptc.id;
                    j["name"] = ptc.name;
                    j["arguments"] = ptc.arguments;
                    tcsJson.push_back(j);
                }

                if (m_persistence && m_sessionDbId > 0) {
                    m_persistence->appendMessage(m_sessionDbId,
                        m_subSessionId, m_seq++, "assistant",
                        "", tcsJson.dump(), "", "", "");
                }
            } else if (!m_accumText.empty()) {
                // Text response — goal complete
                xFinishGoal(m_accumText);
            } else {
                // Empty response
                const auto& complete = std::get<mpsc::Complete>(ev);
                xFinishGoal(complete.text);
            }
        } else if (std::holds_alternative<mpsc::Error>(ev)) {
            const auto& err = std::get<mpsc::Error>(ev);
            xFailGoal(err.message);
        }
    }
}

void DrivenCore::xExecuteTools() {
    if (m_pendingToolCalls.empty()) {
        m_state = CoreState::Idle;
        return;
    }

    if (m_turnCount >= MAX_TURNS) {
        xFailGoal("ERROR: max tool call turns (" +
                  std::to_string(MAX_TURNS) + ") exceeded");
        return;
    }

    // Add assistant message with tool calls to conversation history
    Message asstMsg("assistant", "");
    for (const auto& ptc : m_pendingToolCalls) {
        ToolCall tc;
        tc.id = ptc.id;
        tc.name = ptc.name;
        tc.arguments = ptc.arguments;
        asstMsg.toolCalls.push_back(tc);
    }
    m_messages.push_back(asstMsg);

    // Build invocations for DependencyGraph
    std::vector<ToolInvocation> invocations;
    for (const auto& ptc : m_pendingToolCalls) {
        auto dit = m_dispatch.find(ptc.name);
        if (dit != m_dispatch.end()) {
            invocations.push_back({dit->second, ptc.arguments,
                                   &m_seq, ptc.id, m_subSessionId});
        } else {
            invocations.push_back({ptc.name, ptc.arguments,
                                   &m_seq, ptc.id, m_subSessionId});
        }
    }

    // Execute via DependencyGraph batching
    auto batches = DependencyGraph::buildBatches(invocations);
    auto batchResults = DependencyGraph::executeBatches(
        batches, m_skillMgr, 4);

    // Process results
    // Flatten batch results back to message order
    size_t invIdx = 0;
    for (const auto& br : batchResults) {
        for (size_t i = 0; i < br.outputs.size(); ++i) {
            std::string result = br.outputs[i];
            if (!br.errors.empty() && i < br.errors.size() &&
                !br.errors[i].empty()) {
                result = br.errors[i] + "\n" + result;
            }
            std::string safeOutput = xSanitizeUtf8(xTruncateForLLM(result));
            m_messages.push_back({"tool", safeOutput,
                                  m_pendingToolCalls[invIdx].id});

            // Persist tool result
            if (m_persistence && m_sessionDbId > 0) {
                m_persistence->appendMessage(m_sessionDbId,
                    m_subSessionId, m_seq++, "tool", safeOutput,
                    "", m_pendingToolCalls[invIdx].id, "", "");
            }
            ++invIdx;
        }
    }

    m_turnCount++;
    m_pendingToolCalls.clear();
    m_accumText.clear();

    // Start next LLM request with tools included (tool-calling follow-up)
    xStartLlmRequest(true);
}

void DrivenCore::xFinishGoal(const std::string& text) {
    std::string safeText = xSanitizeUtf8(text);

    // Persist final response
    if (m_persistence && m_sessionDbId > 0) {
        m_persistence->appendMessage(m_sessionDbId,
            m_subSessionId, m_seq++, "assistant", safeText, "", "", "", "");
    }

    m_messages.clear();
    m_toolSchemas.clear();
    m_pendingToolCalls.clear();
    m_accumText.clear();
    m_state = CoreState::Idle;
}

void DrivenCore::xFailGoal(const std::string& error) {
    if (m_persistence && m_sessionDbId > 0) {
        m_persistence->appendMessage(m_sessionDbId,
            m_subSessionId, m_seq++, "assistant", error, "", "", "", "");
    }

    m_messages.clear();
    m_toolSchemas.clear();
    m_pendingToolCalls.clear();
    m_accumText.clear();
    m_state = CoreState::Idle;
}

void DrivenCore::cancel() {
    m_provider->cancel();
    m_state = CoreState::Idle;
    m_messages.clear();
    m_toolSchemas.clear();
    m_pendingToolCalls.clear();
    m_accumText.clear();
}

void DrivenCore::xPersistMessage(const std::string& role,
                                  const std::string& content,
                                  const std::string& toolCallId,
                                  const std::vector<ToolCall>& toolCalls) {
    if (!m_persistence || m_sessionDbId <= 0) return;

    std::string tcsJson;
    if (!toolCalls.empty()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& tc : toolCalls) {
            arr.push_back({{"id", tc.id},
                           {"name", tc.name},
                           {"arguments", tc.arguments}});
        }
        tcsJson = arr.dump();
    }

    m_persistence->appendMessage(m_sessionDbId,
        m_subSessionId, m_seq++, role, content, tcsJson,
        toolCallId, "", "");
}

} // namespace a0
