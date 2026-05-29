#include "agent_core.h"
#include "base_prompt.h"
#include "persistence/persistence_store.h"
#include "persistence/build_identity.h"
#include "trace.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

using a0::persistence::BuildIdentity;

// ---------------------------------------------------------------------------
// UTF-8 sanitization: replace invalid bytes with '?' so nlohmann/json
// doesn't throw type_error 316 on tool results.
// ---------------------------------------------------------------------------

static std::string sanitizeUtf8(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c <= 0x7F) {
            out += c;
        } else if (c >= 0xC0 && c <= 0xDF) {
            // 2-byte sequence
            if (i + 1 < raw.size() &&
                (static_cast<unsigned char>(raw[i+1]) & 0xC0) == 0x80) {
                out += raw[i]; out += raw[i+1]; ++i;
            } else {
                out += '?';
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            // 3-byte sequence
            if (i + 2 < raw.size() &&
                (static_cast<unsigned char>(raw[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(raw[i+2]) & 0xC0) == 0x80) {
                out += raw[i]; out += raw[i+1]; out += raw[i+2]; i += 2;
            } else {
                out += '?';
            }
        } else if (c >= 0xF0 && c <= 0xF7) {
            // 4-byte sequence
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

DefaultAgentCore::DefaultAgentCore(SkillRegistry* registry,
                                    ToolRunner* toolRunner,
                                    SkillRunner* skillRunner,
                                    InferenceProvider* provider,
                                    ContextManager* context,
                                    InvocationLogger* logger,
                                    DependencyResolver* depResolver,
                                    SchemaInferenceEngine* inferenceEngine,
                                    a0::SystemToolRegistry* systemTools,
                                    a0::persistence::PersistenceStore* persistence,
                                    DockerToolRunner* dockerRunner,
                                    ComposeManager* composeMgr)
    : m_registry(registry)
    , m_toolRunner(toolRunner)
    , m_systemTools(systemTools)
    , m_persistence(persistence)
    , m_dockerRunner(dockerRunner)
    , m_composeMgr(composeMgr)
    , m_skillRunner(skillRunner)
    , m_provider(provider)
    , m_context(context)
    , m_logger(logger)
    , m_depResolver(depResolver)
    , m_inferenceEngine(inferenceEngine)
    , m_initialized(false) {}

bool DefaultAgentCore::init(const std::string& skillsDir) {
    TRACE_LOG("init(" << skillsDir << ")");
    if (!m_registry->loadFromDirectory(skillsDir)) {
        return false;
    }

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::ostringstream ss;
    ss << "session_" << ms;
    m_sessionId = ss.str();

    // Build base prompt once at init
    m_basePrompt = a0::buildBasePrompt(m_registry);

    // Register agent binary fingerprint with persistence store
    if (m_persistence) {
        try {
            a0::persistence::BuildFingerprint fp;
            fp.binarySha1 = BuildIdentity::binarySha1();
            BuildIdentity::detectGit(skillsDir, fp);
            m_agentDbId = m_persistence->registerAgent(fp);
        } catch (const std::exception& e) {
            std::cerr << "a0: persistence init failed: " << e.what() << std::endl;
            return false;
        }
    }

    m_initialized = true;
    return true;
}

void DefaultAgentCore::xLogAndPush(const std::string& goal, const json& result) {
    m_context->push({"assistant", result.is_string() ? result.get<std::string>() : result.dump()});

    LogEntry entry;
    entry.sessionId = m_sessionId;
    entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    entry.eventType = "process_goal";
    entry.data = json{{"goal", goal}, {"result", result}}.dump();
    m_logger->log(entry);
}

// Truncate a tool result for LLM consumption to avoid blowing up context
static std::string truncateForLLM(const std::string& output, size_t maxBytes = 8192) {
    if (output.size() <= maxBytes) return output;
    std::string truncated = output.substr(0, maxBytes);
    truncated += "\n... (output truncated at " + std::to_string(maxBytes) + " bytes)";
    return truncated;
}

json DefaultAgentCore::processGoal(const std::string& goal) {
    TRACE_LOG("processGoal(" << goal << ")");
    if (!m_initialized) {
        throw std::logic_error("AgentCore not initialized");
    }
    if (goal.empty()) {
        return "no goal provided";
    }

    m_context->push({"user", goal});

    // Persistence: create a session for this goal processing
    if (m_persistence && m_agentDbId > 0) {
        m_sessionDbId = m_persistence->createSession(0, 0, m_agentDbId);
        m_persistence->appendMessage(m_sessionDbId, "user", goal, "", "", "", "");
    }

    // Check for exact skill name match (backward compat with SkillRunner)
    for (const auto& name : m_registry->listPrompts()) {
        if (goal == name) {
            auto promptOpt = m_registry->getPrompt(name);
            if (promptOpt.has_value()) {
                auto result = m_skillRunner->execute(*promptOpt, {{"goal", goal}});
                xLogAndPush(goal, result);
                if (m_persistence && m_sessionDbId > 0)
                    m_persistence->endSession(m_sessionDbId);
                return result;
            }
        }
    }

    // No exact match — try tool-calling loop if system tools available
    auto toolSchemas = m_systemTools ? m_systemTools->schemas()
                                     : std::vector<ToolSchema>();

    if (!toolSchemas.empty()) {
        std::vector<Message> messages;
        messages.push_back({"user", goal});

        const int maxTurns = 10;
        const size_t maxPayloadBytes = 512 * 1024; // 512 KB total

        for (int turn = 0; turn < maxTurns; ++turn) {
            auto response = m_provider->complete(m_basePrompt, messages, toolSchemas);

            if (!response.toolCalls.empty()) {
                // Insert assistant message with tool_calls before results
                Message asstMsg("assistant", "");
                asstMsg.toolCalls = response.toolCalls;
                messages.push_back(asstMsg);

                for (auto& tc : response.toolCalls) {
                    TRACE_LOG("tool_call[" << turn << "]: " << tc.name
                              << " args=" << tc.arguments.dump());
                    auto result = m_systemTools->execute(tc.name, tc.arguments);

                    // Truncate + sanitize tool output for LLM context limits
                    std::string safeOutput = sanitizeUtf8(truncateForLLM(result.output));
                    messages.push_back({"tool", safeOutput, tc.id});

                    // Persist tool call + result
                    if (m_persistence && m_sessionDbId > 0) {
                        m_persistence->appendMessage(m_sessionDbId, "tool_call", "",
                                                      tc.arguments.dump(), tc.id,
                                                      tc.name, safeOutput);
                    }

                    // Check cumulative payload size
                    size_t totalBytes = 0;
                    for (const auto& m : messages)
                        totalBytes += m.content.size();
                    if (totalBytes > maxPayloadBytes) {
                        std::string err = "ERROR: cumulative message payload (" +
                            std::to_string(totalBytes) + " bytes) exceeds limit";
                        if (m_persistence && m_sessionDbId > 0)
                            m_persistence->endSession(m_sessionDbId);
                        return json(err);
                    }
                }
                continue;
            }

            if (!response.content.empty()) {
                auto result = json(sanitizeUtf8(response.content));
                xLogAndPush(goal, result);
                if (m_persistence && m_sessionDbId > 0)
                    m_persistence->endSession(m_sessionDbId);
                return result;
            }

            return json("ERROR: LLM returned empty response");
        }

        if (m_persistence && m_sessionDbId > 0)
            m_persistence->endSession(m_sessionDbId);
        return json("ERROR: max tool call turns (" + std::to_string(maxTurns) + ") exceeded");
    }

    // Fallback: use SchemaInferenceEngine (backward compat for tests / no system tools)
    json result;
    try {
        auto prompt = m_inferenceEngine->inferPrompt(goal);
        m_registry->addPrompt(prompt);
        result = m_skillRunner->execute(prompt, {{"goal", goal}});
    } catch (const std::exception& e) {
        result = json("failed to infer prompt: " + std::string(e.what()));
    }
    xLogAndPush(goal, result);
    if (m_persistence && m_sessionDbId > 0)
        m_persistence->endSession(m_sessionDbId);
    return result;
}

json DefaultAgentCore::runSkill(const std::string& skillName, const json& params) {
    TRACE_LOG("runSkill(" << skillName << ")");
    if (!m_initialized) {
        throw std::logic_error("AgentCore not initialized");
    }

    auto promptOpt = m_registry->getPrompt(skillName);
    if (!promptOpt.has_value()) {
        return json("prompt not found: " + skillName);
    }

    Prompt prompt = *promptOpt;
    auto missing = m_depResolver->missingDependencies(prompt);
    if (!missing.empty()) {
        std::string err = "Missing dependencies: ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) err += ", ";
            err += missing[i];
        }
        return json(err);
    }

    json execParams = params;
    json result = m_skillRunner->execute(prompt, execParams);

    LogEntry entry;
    entry.sessionId = m_sessionId;
    entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    entry.eventType = "run_skill";
    entry.data = json{{"skill", skillName}, {"params", params}, {"result", result}}.dump();
    m_logger->log(entry);

    return result;
}

std::string DefaultAgentCore::currentSessionId() const {
    return m_sessionId;
}

bool DefaultAgentCore::resumeSession(const std::string& sessionId) {
    TRACE_LOG("resumeSession(" << sessionId << ")");
    m_sessionId = sessionId;
    bool found = false;
    m_logger->replay(sessionId, [this, &found](const LogEntry& entry) {
        found = true;
        try {
            json j = json::parse(entry.data);
            if (j.contains("goal")) {
                m_context->push({"user", j["goal"].get<std::string>()});
            }
            if (j.contains("result")) {
                std::string res = j["result"].is_string() ? j["result"].get<std::string>() : j["result"].dump();
                m_context->push({"assistant", res});
            }
        } catch (...) {}
    });
    m_initialized = true;
    return found;
}

void DefaultAgentCore::run() {
    TRACE_LOG("run()");
    if (!m_initialized) return;

    std::string line;
    while (std::getline(std::cin, line)) {
        auto result = processGoal(line);
        std::cout << result.dump() << std::endl;
    }
}
