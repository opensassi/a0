#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using json = nlohmann::json;

struct Tool {
    std::string name;
    std::string description;
    std::string command;
    // "stdin" = pipe input via stdin, "args" = pass as CLI arguments
    std::string inputMode = "stdin";
};

struct ValidatorBinding {
    std::string toolName;
    std::optional<std::string> transform; // future: JSONPath binding
};

struct Skill {
    std::string name;
    std::string description;
    std::string prompt;
    std::vector<std::string> dependencies;    // tool and skill names required
    std::vector<ValidatorBinding> validators; // post-LLM processing chain
};

class ComponentRegistry {
public:
    virtual ~ComponentRegistry() = default;
    virtual bool loadFromDirectory(const std::string& path) = 0;
    virtual std::optional<Tool> getTool(const std::string& name) const = 0;
    virtual std::optional<Skill> getSkill(const std::string& name) const = 0;
    virtual std::vector<std::string> listTools() const = 0;
    virtual std::vector<std::string> listSkills() const = 0;
    virtual bool addTool(const Tool& tool) = 0;
    virtual bool addSkill(const Skill& skill) = 0;
};

class ToolRunner {
public:
    virtual ~ToolRunner() = default;
    virtual json run(const Tool& tool, const json& params) = 0;
};

class InferenceProvider {
public:
    virtual ~InferenceProvider() = default;
    virtual std::string complete(const std::string& systemPrompt,
                                  const std::string& userPrompt) = 0;
    virtual void setMockUrl(const std::string& url) = 0;
};

struct ContextFrame {
    std::string role;
    std::string content;
};

class ContextManager {
public:
    virtual ~ContextManager() = default;
    virtual void push(const ContextFrame& frame) = 0;
    virtual ContextFrame pop() = 0;
    virtual ContextFrame peek() const = 0;
    virtual size_t size() const = 0;
    virtual void clear() = 0;
    virtual std::vector<ContextFrame> snapshot() const = 0;
};

struct LogEntry {
    std::string sessionId;
    int64_t timestamp;
    std::string eventType;
    std::string data;
};

class InvocationLogger {
public:
    virtual ~InvocationLogger() = default;
    virtual void log(const LogEntry& entry) = 0;
    virtual bool replay(const std::string& sessionId,
                        std::function<void(const LogEntry&)> callback) = 0;
    virtual std::vector<std::string> listSessions() const = 0;
};

class DependencyResolver {
public:
    virtual ~DependencyResolver() = default;
    virtual bool checkToolDependencies(const Tool& tool) const = 0;
    virtual bool checkSkillDependencies(const Skill& skill) const = 0;
    virtual std::vector<std::string> missingDependencies(const Skill& skill) const = 0;
};

class SkillRunner {
public:
    virtual ~SkillRunner() = default;
    virtual std::string expandPrompt(const Skill& skill, const json& params) = 0;
    virtual json runValidators(const Skill& skill, const json& input) = 0;
    virtual json execute(const Skill& skill, const json& params) = 0;
};

class SchemaInferenceEngine {
public:
    virtual ~SchemaInferenceEngine() = default;
    virtual Tool inferTool(const std::string& naturalLanguageDescription) = 0;
    virtual Skill inferSkill(const std::string& naturalLanguageDescription) = 0;
};

class AgentCore {
public:
    virtual ~AgentCore() = default;
    virtual bool init(const std::string& componentsDir) = 0;
    virtual json processGoal(const std::string& goal) = 0;
    virtual bool resumeSession(const std::string& sessionId) = 0;
    virtual std::string currentSessionId() const = 0;
    virtual void run() = 0;
};
