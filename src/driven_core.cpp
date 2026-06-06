#include "driven_core.h"
#include "base_prompt.h"
#include "dependency_graph.h"
#include "persistence/persistence_store.h"
#include "trace.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

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

static std::string xTruncateForLLM(const std::string& output, size_t maxBytes = 65536) {
    if (output.size() <= maxBytes) return output;
    std::string truncated = output.substr(0, maxBytes);
    truncated += "\n... (output truncated at " + std::to_string(maxBytes) + " bytes)";
    return truncated;
}

namespace a0 {

DrivenCore::DrivenCore(LlmProvider* provider,
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

    m_messages.push_back({"user", goal});
    xBuildToolSchemas();

    if (m_persistence && m_sessionDbId > 0) {
        m_persistence->appendMessage(m_sessionDbId, std::nullopt, m_seq++,
            "user", goal, "", "", "", "");
    }

    xStartLlmRequest(true);
    m_state = CoreState::AwaitingLlm;
}

std::string DrivenCore::runSync(const std::string& goal) {
    m_lastResult.clear();
    submitGoal(goal);
    while (!idle()) {
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return m_lastResult;
}

void DrivenCore::xBuildToolSchemas() {
    m_toolSchemas.clear();
    m_dispatch.clear();

    if (!m_skillMgr) return;

    m_dispatch = m_skillMgr->buildDispatchTable();
    m_toolSchemas = m_skillMgr->schemas(true);

    for (const auto& [shortName, qualified] : m_dispatch) {
        bool alreadyIn = false;
        for (const auto& ts : m_toolSchemas) {
            if (ts.name == shortName) { alreadyIn = true; break; }
        }
        if (alreadyIn) continue;

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
    std::string systemPrompt = a0::buildBasePrompt(m_skillMgr);

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
        // Streaming Complete events carry empty text (text was in LlmToken events).
        // Fill in the actual result text so consumers (cmdRun) get the answer.
        for (auto& ev : events) {
            if (auto* c = std::get_if<mpsc::Complete>(&ev)) {
                if (c->text.empty() && !m_lastResult.empty())
                    c->text = m_lastResult;
            }
        }
        return events;
    }

    if (m_state == CoreState::ExecutingTools) {
        return xExecuteTools();
    }

    return {};
}

void DrivenCore::xHandleLlmEvents(const std::vector<mpsc::AppCoreEvent>& events) {
    TRACE_LOG("DrivenCore::xHandleLlmEvents count=" << events.size());
    for (const auto& ev : events) {
        if (std::holds_alternative<mpsc::LlmToken>(ev)) {
            m_accumText += std::get<mpsc::LlmToken>(ev).text;
        } else if (std::holds_alternative<mpsc::ToolStart>(ev)) {
            const auto& ts = std::get<mpsc::ToolStart>(ev);
            PendingToolCall ptc;
            ptc.id = ts.id;
            ptc.name = ts.toolName;
            try {
                ptc.arguments = nlohmann::json::parse(ts.arguments);
            } catch (...) {
                ptc.arguments = ts.arguments;
            }
            m_pendingToolCalls.push_back(std::move(ptc));
        } else if (std::holds_alternative<mpsc::Complete>(ev)) {
            if (!m_pendingToolCalls.empty()) {
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
                xFinishGoal(m_accumText);
            } else {
                const auto& complete = std::get<mpsc::Complete>(ev);
                xFinishGoal(complete.text);
            }
        } else if (std::holds_alternative<mpsc::Error>(ev)) {
            const auto& err = std::get<mpsc::Error>(ev);
            xFailGoal(err.message);
        }
    }
}

std::vector<mpsc::AppCoreEvent> DrivenCore::xExecuteTools() {
    std::vector<mpsc::AppCoreEvent> events;

    if (m_pendingToolCalls.empty()) {
        m_state = CoreState::Idle;
        return events;
    }

    if (m_turnCount >= MAX_TURNS) {
        xFailGoal("ERROR: max tool call turns (" +
                  std::to_string(MAX_TURNS) + ") exceeded");
        return events;
    }

    Message asstMsg("assistant", "");
    for (const auto& ptc : m_pendingToolCalls) {
        ToolCall tc;
        tc.id = ptc.id;
        tc.name = ptc.name;
        tc.arguments = ptc.arguments;
        asstMsg.toolCalls.push_back(tc);
    }
    m_messages.push_back(asstMsg);

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

    auto batches = DependencyGraph::buildBatches(invocations);
    auto batchResults = DependencyGraph::executeBatches(
        batches, m_skillMgr, 4);

    size_t invIdx = 0;
    for (const auto& br : batchResults) {
        for (size_t i = 0; i < br.outputs.size(); ++i) {
            std::string result = br.outputs[i];
            if (!br.errors.empty() && i < br.errors.size() &&
                !br.errors[i].empty()) {
                result = br.errors[i] + "\n" + result;
            }
            std::string safeOutput = xSanitizeUtf8(xTruncateForLLM(result));

            mpsc::ToolEnd te;
            te.toolName = m_pendingToolCalls[invIdx].name;
            te.exitCode = br.errors.empty() || i >= br.errors.size() || br.errors[i].empty() ? 0 : 1;
            te.output = safeOutput;
            events.push_back(std::move(te));

            m_messages.push_back({"tool", safeOutput,
                                  m_pendingToolCalls[invIdx].id});

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

    xStartLlmRequest(true);
    return events;
}

void DrivenCore::xFinishGoal(const std::string& text) {
    std::string safeText = xSanitizeUtf8(text);
    m_lastResult = safeText;

    if (m_persistence && m_sessionDbId > 0) {
        m_persistence->appendMessage(m_sessionDbId,
            m_subSessionId, m_seq++, "assistant", safeText, "", "", "", "");
    }

    m_toolSchemas.clear();
    m_pendingToolCalls.clear();
    m_accumText.clear();
    m_state = CoreState::Idle;
}

void DrivenCore::xFailGoal(const std::string& error) {
    m_lastResult = "ERROR: " + error;

    if (m_persistence && m_sessionDbId > 0) {
        m_persistence->appendMessage(m_sessionDbId,
            m_subSessionId, m_seq++, "assistant", error, "", "", "", "");
    }

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
