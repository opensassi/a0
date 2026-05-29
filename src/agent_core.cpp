#include "agent_core.h"
#include "trace.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

DefaultAgentCore::DefaultAgentCore(SkillRegistry* registry,
                                    ToolRunner* toolRunner,
                                    SkillRunner* skillRunner,
                                    InferenceProvider* provider,
                                    ContextManager* context,
                                    InvocationLogger* logger,
                                    DependencyResolver* depResolver,
                                    SchemaInferenceEngine* inferenceEngine,
                                    DockerToolRunner* dockerRunner,
                                    ComposeManager* composeMgr)
    : m_registry(registry)
    , m_toolRunner(toolRunner)
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
    m_initialized = true;
    return true;
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

    // look for a matching prompt by exact name (case-sensitive)
    std::string matchedPromptName;
    for (const auto& name : m_registry->listPrompts()) {
        if (goal == name) {
            matchedPromptName = name;
            break;
        }
    }

    Prompt prompt;
    bool foundExact = false;
    if (!matchedPromptName.empty()) {
        auto promptOpt = m_registry->getPrompt(matchedPromptName);
        if (promptOpt.has_value()) {
            prompt = *promptOpt;
            foundExact = true;
        }
    }

    json result;
    if (!foundExact) {
        try {
            prompt = m_inferenceEngine->inferPrompt(goal);
            m_registry->addPrompt(prompt);
        } catch (const std::exception& e) {
            result = "failed to infer prompt: " + std::string(e.what());
        }
    }

    if (result.is_null()) {
        auto missing = m_depResolver->missingDependencies(prompt);
        if (!missing.empty()) {
            std::string err = "Missing dependencies: ";
            for (size_t i = 0; i < missing.size(); ++i) {
                if (i > 0) err += ", ";
                err += missing[i];
            }
            result = err;
        } else {
            result = m_skillRunner->execute(prompt, {{"goal", goal}});
        }
    }

    m_context->push({"assistant", result.is_string() ? result.get<std::string>() : result.dump()});

    LogEntry entry;
    entry.sessionId = m_sessionId;
    entry.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    entry.eventType = "process_goal";
    entry.data = json{{"goal", goal}, {"result", result}}.dump();
    m_logger->log(entry);

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
