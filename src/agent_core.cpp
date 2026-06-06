#include "agent_core.h"
#include "base_prompt.h"
#include "skills/skills.h"
#include "dependency_graph.h"
#include "persistence/persistence_store.h"
#include "persistence/build_identity.h"
#include "hex_session_id.h"
#include "trace.h"
#include <unistd.h>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

using a0::persistence::BuildIdentity;
using a0::skills::SkillTool;
namespace fs = std::filesystem;

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

DefaultAgentCore::DefaultAgentCore(ToolRunner* toolRunner,
                                    SkillRunner* skillRunner,
                                    ContextManager* context,
                                    DependencyResolver* depResolver,
                                    a0::skills::SkillManager* skillMgr,
                                    a0::persistence::PersistenceStore* persistence,
                                    DockerToolRunner* dockerRunner,
                                    ComposeManager* composeMgr)
    : m_skillMgr(skillMgr)
    , m_toolRunner(toolRunner)
    , m_persistence(persistence)
    , m_dockerRunner(dockerRunner)
    , m_composeMgr(composeMgr)
    , m_skillRunner(skillRunner)
    , m_context(context)
    , m_depResolver(depResolver)
    , m_initialized(false) {}

bool DefaultAgentCore::init(const std::string& skillsDir) {
    return init(skillsDir, "");
}

bool DefaultAgentCore::init(const std::string& skillsDir, const std::string& a0Dir) {
    TRACE_LOG("init(" << skillsDir << ")");
    m_skillsDir = skillsDir;
    m_a0Dir = a0Dir;

    // Load all skills from disk
    if (m_skillMgr && m_skillMgr->loadAll() != 0) {
        std::cerr << "Warning: SkillManager::loadAll() returned non-zero" << std::endl;
    }

    // Validate all systemTool entries have registered C++ handlers
    if (m_skillMgr) {
        auto missing = m_skillMgr->missingHandlers();
        if (!missing.empty()) {
            for (const auto& qn : missing) {
                std::cerr << "FATAL: system tool '" << qn
                          << "' declared in skill.json but has no C++ handler." << std::endl;
            }
            std::cerr << "Register handlers via SkillManager::registerHandler() in main.cpp." << std::endl;
            return false;
        }
    }

    // Ensure external/a0 repo for self-development and tool scripts
    if (!m_a0Dir.empty() && m_skillMgr && !m_externalRepoUrl.empty()) {
        std::string externalDir = m_a0Dir + "/external/a0";
        if (!fs::is_directory(externalDir)) {
            std::cerr << "a0: cloning " << m_externalRepoUrl << " into " << externalDir << std::endl;
            json cloneParams = {
                {"args", json::array({
                    "clone", "--depth", "1", "-b", "main",
                    m_externalRepoUrl, externalDir
                })}
            };
            auto result = m_skillMgr->executeTool("system-git-clone", cloneParams);
            std::string output = result.is_string() ? result.get<std::string>() : result.dump();
            if (output.find("ERROR:") == 0) {
                std::cerr << "Warning: git clone failed: " << output << std::endl;
            }
        } else {
            // Fetch latest main for existing clone
            json fetchParams = {{"args", json::array({"-C", externalDir, "fetch", "origin", "main"})}};
            m_skillMgr->executeTool("system-git-fetch", fetchParams);
            json checkoutParams = {{"args", json::array({"-C", externalDir, "checkout", "main"})}};
            m_skillMgr->executeTool("system-git-checkout", checkoutParams);
            json resetParams = {{"args", json::array({"-C", externalDir, "reset", "--hard", "origin/main"})}};
            m_skillMgr->executeTool("system-git-reset", resetParams);
        }
        m_skillRunner->setGlobalVar("A0_SRC_DIR", externalDir);
    }

    // Apply skill-level CLI args to ToolState (keyed "args:<skill>-<arg>")
    if (!m_skillArgs.empty() && m_skillMgr) {
        for (const auto& [key, val] : m_skillArgs) {
            m_skillMgr->toolState().set("args:" + key, json(val));
            m_skillRunner->setGlobalVar(key, val);
        }
    }

    if (m_sessionId.empty()) {
        m_sessionId = generateHexSessionId();
    }

    // Build base prompt once at init
    m_basePrompt = a0::buildBasePrompt(m_skillMgr);

    // Set global template variables
    m_skillRunner->setGlobalVar("SESSION_ID", m_sessionId);
    m_skillRunner->setGlobalVar("SESSIONS_DIR", "sessions");
    m_skillRunner->setGlobalVar("A0_DIR", ".a0");
    char cwdBuf[4096];
    if (::getcwd(cwdBuf, sizeof(cwdBuf))) {
        m_skillRunner->setGlobalVar("PROJECT_DIR", cwdBuf);
    }

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

void DefaultAgentCore::setSession(const std::string& sessionId,
                                   int64_t sessionDbId,
                                   a0::SessionContext* sessionCtx) {
    m_sessionId = sessionId;
    m_sessionDbId = sessionDbId;
    m_sessionCtx = sessionCtx;

    m_skillRunner->setGlobalVar("SESSION_ID", m_sessionId);
    if (m_sessionCtx) {
        char buf[4096];
        if (::getcwd(buf, sizeof(buf))) {
            m_skillRunner->setGlobalVar("PROJECT_DIR", buf);
        }
    }
}

bool DefaultAgentCore::ensureSession() {
    if (m_sessionDbId > 0) return true;
    if (!m_persistence || m_agentDbId <= 0) return false;
    if (m_sessionId.empty())
        m_sessionId = generateHexSessionId();
    m_sessionDbId = m_persistence->createSession(m_sessionId, 0, 0, m_agentDbId);
    m_skillRunner->setGlobalVar("SESSION_ID", m_sessionId);
    return m_sessionDbId > 0;
}

void DefaultAgentCore::xPushToContext(const std::string& goal, const json& result) {
    (void)goal;
    m_context->push({"assistant", result.is_string() ? result.get<std::string>() : result.dump()});
}

// Truncate a tool result for LLM consumption to avoid blowing up context
static std::string truncateForLLM(const std::string& output, size_t maxBytes = 8192) {
    if (output.size() <= maxBytes) return output;
    std::string truncated = output.substr(0, maxBytes);
    truncated += "\n... (output truncated at " + std::to_string(maxBytes) + " bytes)";
    return truncated;
}

// ---------------------------------------------------------------------------
// Build dispatch table — maps LLM-facing short names to qualified internal names
// ---------------------------------------------------------------------------

void DefaultAgentCore::xBuildDispatchTable() {
    m_dispatch.clear();
    if (m_skillMgr) {
        m_dispatch = m_skillMgr->buildDispatchTable();
    }
}

// ---------------------------------------------------------------------------
// processGoal
// ---------------------------------------------------------------------------

std::string DefaultAgentCore::xRunForkedLoop(
    const std::string& userInput,
    const std::vector<ToolSchema>& tools,
    int maxTurns)
{
    // Local messages vector — this IS the fork/rewind mechanism.
    // Everything in this vector is discarded when the function returns.
    std::vector<Message> messages;
    messages.push_back({"user", userInput});

    // Fork sub-session: persist each turn to SQLite for traceability
    int64_t subSessionId = m_nextSubSession++;
    int subSeq = 0;

    // Auto-analyze: inject tools_for_prompt result and accumulate validated tools.
    m_accumulatedTools.clear();
    if (m_skillMgr) {
        auto analysis = m_skillMgr->executeToolWithMeta(
            "system-meta-tools_for_prompt", {{"prompt", userInput}});
        if (!analysis.output.empty()) {
            messages.push_back({"assistant", analysis.output});
            for (const auto& t : analysis.recommendedTools)
                m_accumulatedTools.insert(t);
        }
    }

    const size_t maxPayloadBytes = 512 * 1024;

    for (int turn = 0; turn < maxTurns; ++turn) {
        std::vector<ToolSchema> combinedSchemas = tools;

        // Add accumulated (validated via tools_for_prompt) tools not already in schemas
        for (const auto& shortName : m_accumulatedTools) {
            bool alreadyIn = false;
            for (const auto& ts : combinedSchemas) {
                if (ts.name == shortName) { alreadyIn = true; break; }
            }
            if (alreadyIn) continue;

            auto dit = m_dispatch.find(shortName);
            if (dit != m_dispatch.end()) {
                ToolSchema ts;
                ts.name = shortName;
                a0::skills::SkillTool st;
                if (m_skillMgr && m_skillMgr->getTool(dit->second, st) == 0) {
                    ts.description = st.description;
                } else {
                    Prompt sp;
                    if (m_skillMgr && m_skillMgr->getPrompt(dit->second, sp) == 0) {
                        ts.description = sp.description;
                    }
                }
                ts.inputSchema = {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}};
                combinedSchemas.push_back(ts);
            }
        }

        auto response = m_provider->complete(m_basePrompt, messages, combinedSchemas);

        if (!response.toolCalls.empty()) {
            // First, check if any tool call resolves to a prompt expansion.
            // If so, expand the prompt and replace the user message with it,
            // then continue the loop — the LLM will see the expanded instructions
            // and can respond with tool calls.
            bool promptExpanded = false;
            std::string expandedText;
            for (const auto& tc : response.toolCalls) {
                auto dit = m_dispatch.find(tc.name);
                if (dit != m_dispatch.end()) {
                    const std::string& qn = dit->second;
                    // If it's a registered tool (system or command), skip prompt expansion
                    SkillTool st;
                    if (m_skillMgr && m_skillMgr->getTool(qn, st) == 0)
                        continue;
                    // Not a tool — try prompt expansion
                    Prompt resolved;
                    if (m_skillMgr && m_skillMgr->getPromptResolved(qn, resolved) == 0) {
                        expandedText = m_skillRunner->expandPrompt(resolved, tc.arguments);
                        promptExpanded = true;
                        break;
                    }
                }
            }

            if (promptExpanded) {
                // Reset messages to just the expanded prompt — start fresh
                // so tools_for_prompt re-analyzes the skill instructions correctly
                // rather than re-recommending the same prompt.
                messages.clear();
                messages.push_back({"user", expandedText});
                continue;
            }

            json tcsJson = json::array();
            for (const auto& tc : response.toolCalls) {
                json j;
                j["id"] = tc.id;
                j["name"] = tc.name;
                j["arguments"] = tc.arguments;
                tcsJson.push_back(j);
            }
            std::string tcsJsonStr = tcsJson.dump();
            Message asstMsg("assistant", "");
            asstMsg.toolCalls = response.toolCalls;
            messages.push_back(asstMsg);

            // Persist assistant turn in fork branch (LLM intent, not tool execution)
            if (m_persistence && m_sessionDbId > 0) {
                m_persistence->appendMessage(m_sessionDbId, subSessionId, subSeq++,
                    "assistant", "", tcsJsonStr, "", "", "");
            }

            // Collect tool calls into DependencyGraph invocations for batched execution
            std::vector<a0::ToolInvocation> invocations;
            invocations.reserve(response.toolCalls.size());
            for (auto& tc : response.toolCalls) {
                TRACE_LOG("forked_tool_call[" << turn << "]: " << tc.name
                          << " args=" << tc.arguments.dump());
                auto dit = m_dispatch.find(tc.name);
                if (dit != m_dispatch.end()) {
                    invocations.push_back({dit->second, tc.arguments,
                                           &subSeq, tc.id, subSessionId});
                } else {
                    // Unknown tool — create a synthetic invocation that will error
                    invocations.push_back({tc.name, tc.arguments,
                                           &subSeq, tc.id, subSessionId});
                }
            }

            // Execute invocations through DependencyGraph batching
            auto batches = a0::DependencyGraph::buildBatches(invocations);
            auto batchResults = a0::DependencyGraph::executeBatches(
                batches, m_skillMgr, m_maxParallel);

            // Flatten batch results back into message order (same as invocations)
            size_t invIdx = 0;
            for (const auto& br : batchResults) {
                for (size_t i = 0; i < br.outputs.size(); ++i) {
                    std::string result = br.outputs[i];
                    if (!br.errors.empty() && i < br.errors.size() &&
                        !br.errors[i].empty()) {
                        result = br.errors[i] + "\n" + result;
                    }
                    std::string safeOutput = sanitizeUtf8(truncateForLLM(result));
                    messages.push_back({"tool", safeOutput,
                                        response.toolCalls[invIdx].id});
                    ++invIdx;

                    size_t totalBytes = 0;
                    for (const auto& m : messages)
                        totalBytes += m.content.size();
                    if (totalBytes > maxPayloadBytes) {
                        return "ERROR: cumulative message payload (" +
                            std::to_string(totalBytes) + " bytes) exceeds limit";
                    }
                }
            }
            continue;
        }

        if (!response.content.empty()) {
            // Persist final answer in fork branch
            if (m_persistence && m_sessionDbId > 0) {
                m_persistence->appendMessage(m_sessionDbId,
                    subSessionId, subSeq++, "assistant",
                    sanitizeUtf8(response.content), "", "", "", "");
            }
            return sanitizeUtf8(response.content);
        }

        return "ERROR: LLM returned empty response";
    }

    return "ERROR: max tool call turns (" + std::to_string(maxTurns) + ") exceeded";
}

json DefaultAgentCore::processGoal(const std::string& goal) {
    return processGoal(goal, json::object());
}

json DefaultAgentCore::processGoal(const std::string& goal, const json& params) {
    TRACE_LOG("processGoal(" << goal << ")");
    if (!m_initialized) {
        throw std::logic_error("AgentCore not initialized");
    }
    if (goal.empty()) {
        return "no goal provided";
    }

    m_context->push({"user", goal});

    if (m_persistence && m_agentDbId > 0) {
        ensureSession();
        if (m_sessionDbId > 0) {
            m_persistence->appendMessage(m_sessionDbId, std::nullopt, 0,
                "user", goal, "", "", "", "");
        }
    }

    int mainSeq = 1;

    // Phase 1: Check for exact goal → prompt match in SkillManager.
    // If matched, expand the prompt with params and run it through the
    // tool-calling forked loop so the LLM can execute tools to fulfill
    // the skill instructions.
    if (m_skillMgr) {
        Prompt resolved;
        json phaseParams = params;
        if (!phaseParams.contains("goal")) {
            phaseParams["goal"] = goal;
        }
        if (m_skillMgr->getPromptResolved(goal, resolved) == 0) {
            auto missing = m_depResolver->missingDependencies(resolved);
            if (!missing.empty()) {
                std::string err = "Missing dependencies: " + missing[0];
                for (size_t i = 1; i < missing.size(); ++i)
                    err += ", " + missing[i];
                json finalResult = json(err);
                xPushToContext(goal, finalResult);
                return finalResult;
            }
            std::string expanded = m_skillRunner->expandPrompt(resolved, phaseParams);
            xBuildDispatchTable();
            auto toolSchemas = m_skillMgr ? m_skillMgr->schemas(true)
                                           : std::vector<ToolSchema>();
            std::string result = toolSchemas.empty()
                ? expanded
                : xRunForkedLoop(expanded, toolSchemas, 25);
            json finalResult = json(result);
            xPushToContext(goal, finalResult);
            if (m_persistence && m_sessionDbId > 0) {
                m_persistence->appendMessage(m_sessionDbId, std::nullopt, mainSeq++,
                    "assistant", result, "", "", "", "");
                m_persistence->endSession(m_sessionDbId);
            }
            return finalResult;
        }

        auto allSkills = m_skillMgr->listSkills(std::nullopt);
        for (const auto& comp : allSkills) {
            for (const auto& ns : {"local", "system"}) {
                std::string qn = std::string(ns) + "-" + comp + "-" + goal;
                if (m_skillMgr->getPromptResolved(qn, resolved) == 0) {
                    auto missing = m_depResolver->missingDependencies(resolved);
                    if (!missing.empty()) {
                        std::string err = "Missing dependencies: " + missing[0];
                        for (size_t i = 1; i < missing.size(); ++i)
                            err += ", " + missing[i];
                        json finalResult = json(err);
                        xPushToContext(goal, finalResult);
                        return finalResult;
                    }
                    std::string expanded = m_skillRunner->expandPrompt(resolved, phaseParams);
                    xBuildDispatchTable();
                    auto toolSchemas = m_skillMgr ? m_skillMgr->schemas(true)
                                                   : std::vector<ToolSchema>();
                    std::string result = toolSchemas.empty()
                        ? expanded
                        : xRunForkedLoop(expanded, toolSchemas, 25);
                    json finalResult = json(result);
                    xPushToContext(goal, finalResult);
                    if (m_persistence && m_sessionDbId > 0) {
                        m_persistence->appendMessage(m_sessionDbId, std::nullopt, mainSeq++,
                            "assistant", result, "", "", "", "");
                        m_persistence->endSession(m_sessionDbId);
                    }
                    return finalResult;
                }
            }
        }
    }

    // Clear tool state for a new processing session
    if (m_skillMgr)
        m_skillMgr->toolState().clear();

    // Phase 2: Forked tool-calling loop
    xBuildDispatchTable();

    auto toolSchemas = m_skillMgr ? m_skillMgr->schemas(true)
                                   : std::vector<ToolSchema>();

    if (!toolSchemas.empty()) {
        std::string result = xRunForkedLoop(goal, toolSchemas, 25);
        json finalResult = json(result);
        xPushToContext(goal, finalResult);
        if (m_persistence && m_sessionDbId > 0) {
            m_persistence->appendMessage(m_sessionDbId, std::nullopt, mainSeq++,
                "assistant", result, "", "", "", "");
            m_persistence->endSession(m_sessionDbId);
        }
        return finalResult;
    }

    // Fallback: no tools available to process the goal
    json result = json("No tools available to process goal: " + goal);
    xPushToContext(goal, result);
    if (m_persistence && m_sessionDbId > 0) {
        m_persistence->appendMessage(m_sessionDbId, std::nullopt, mainSeq++,
            "assistant", result.is_string() ? result.get<std::string>() : result.dump(),
            "", "", "", "");
        m_persistence->endSession(m_sessionDbId);
    }
    return result;
}

json DefaultAgentCore::runSkill(const std::string& skillName, const json& params) {
    TRACE_LOG("runSkill(" << skillName << ")");
    if (!m_initialized) {
        throw std::logic_error("AgentCore not initialized");
    }

    if (!m_skillMgr) {
        return json("SkillManager not available");
    }

    // Resolve the prompt via SkillManager (with chain flattening)
    Prompt prompt;
    if (m_skillMgr->getPromptResolved(skillName, prompt) != 0) {
        return json("prompt not found: " + skillName);
    }

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

    return result;
}

std::string DefaultAgentCore::currentSessionId() const {
    return m_sessionId;
}

bool DefaultAgentCore::resumeSession(const std::string& uuid) {
    TRACE_LOG("resumeSession(" << uuid << ")");
    m_sessionId = uuid;
    if (!m_persistence) {
        m_initialized = true;
        return false;
    }
    m_sessionDbId = m_persistence->findSessionByUuid(uuid);
    if (m_sessionDbId <= 0) {
        m_initialized = true;
        return false;
    }
    auto messages = m_persistence->loadMessages(m_sessionDbId, std::nullopt);
    if (messages.empty()) {
        m_initialized = true;
        return false;
    }
    for (const auto& msg : messages) {
        if (msg.role == "user" || msg.role == "assistant") {
            m_context->push({msg.role, msg.content});
        }
    }
    m_initialized = true;
    return true;
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

a0::StreamHandle DefaultAgentCore::processGoalStreaming(
    const std::string& goal, a0::StreamCallback onChunk)
{
    TRACE_LOG("processGoalStreaming(" << goal << ")");

    if (m_persistence && m_sessionDbId > 0) {
        int seq = 0;
        m_persistence->appendMessage(m_sessionDbId, std::nullopt, seq,
            "user", goal, "", "", "", "");
    }

    // Check for exact prompt match first
    if (m_skillMgr) {
        Prompt sp;
        std::string qualified;
        if (goal.find(':') != std::string::npos) {
            qualified = goal;
        } else {
            for (const auto& name : m_skillMgr->listSkills(std::nullopt)) {
                if (name.find(goal) != std::string::npos ||
                    goal.find(name) != std::string::npos) {
                    qualified = name;
                    break;
                }
            }
        }

        if (!qualified.empty() && m_skillMgr->getPrompt(qualified, sp) == 0) {
            json params;
            params["goal"] = goal;
            params["streaming"] = true;
            params["_tool"] = goal;

            return m_skillRunner->executeStreaming(sp, params, std::move(onChunk));
        }
    }

    // Fall back: create a synthetic prompt for streaming mode
    Prompt inferredPrompt;
    inferredPrompt.name = "_streaming";
    inferredPrompt.prompt = goal;

    json params;
    params["goal"] = goal;
    params["streaming"] = true;
    params["_tool"] = goal;

    return m_skillRunner->executeStreaming(inferredPrompt, params, std::move(onChunk));
}
