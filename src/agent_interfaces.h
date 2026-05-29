#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using json = nlohmann::json;

enum class TrustLevel {
    HIGH,
    MEDIUM,
    LOW
};

struct Tool {
    std::string name;
    std::string description;
    std::string command;
    std::string inputMode = "stdin";

    std::string dockerImage;
    TrustLevel trustLevel = TrustLevel::MEDIUM;
    std::vector<std::string> aptDependencies;
};

struct ValidatorBinding {
    std::string toolName;
    std::optional<std::string> transform;
};

struct Prompt {
    std::string name;
    std::string description;
    std::string prompt;
    std::vector<std::string> dependencies;
    std::vector<ValidatorBinding> validators;

    std::string composeFile;
    std::vector<std::string> aptDependencies;
};

class SkillRegistry {
public:
    virtual ~SkillRegistry() = default;
    virtual bool loadFromDirectory(const std::string& path) = 0;
    virtual std::optional<Tool> getTool(const std::string& name) const = 0;
    virtual std::optional<Prompt> getPrompt(const std::string& name) const = 0;
    virtual std::vector<std::string> listTools() const = 0;
    virtual std::vector<std::string> listPrompts() const = 0;
    virtual bool addTool(const Tool& tool) = 0;
    virtual bool addPrompt(const Prompt& prompt) = 0;
};

class ToolRunner {
public:
    virtual ~ToolRunner() = default;
    virtual json run(const Tool& tool, const json& params) = 0;
};

struct ToolCall {
    std::string id;
    std::string name;
    json arguments;
};

struct ToolSchema {
    std::string name;
    std::string description;
    json inputSchema;  // JSON Schema object
};

struct Message {
    std::string role;                 // "system", "user", "assistant", "tool"
    std::string content;
    std::string toolCallId;           // non-empty only for role=="tool"
    std::vector<ToolCall> toolCalls;  // non-empty only for role=="assistant"

    Message() = default;
    Message(const std::string& r, const std::string& c)
        : role(r), content(c) {}
    Message(const std::string& r, const std::string& c, const std::string& tcid)
        : role(r), content(c), toolCallId(tcid) {}
};

struct CompletionResponse {
    std::string content;               // text response (empty if tool_calls)
    std::vector<ToolCall> toolCalls;   // tool calls (empty if text response)
};

class InferenceProvider {
public:
    virtual ~InferenceProvider() = default;

    /// Original single-turn complete (used by SkillRunner template flow)
    virtual std::string complete(const std::string& systemPrompt,
                                  const std::string& userPrompt) = 0;

    /// Tool-calling complete: sends messages + tool schemas, returns text or tool_calls
    virtual CompletionResponse complete(
        const std::string& systemPrompt,
        const std::vector<Message>& messages,
        const std::vector<ToolSchema>& tools) = 0;

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
    virtual bool checkPromptDependencies(const Prompt& prompt) const = 0;
    virtual std::vector<std::string> missingDependencies(const Prompt& prompt) const = 0;
};

class SkillRunner {
public:
    virtual ~SkillRunner() = default;
    virtual std::string expandPrompt(const Prompt& prompt, const json& params) = 0;
    virtual json runValidators(const Prompt& prompt, const json& input) = 0;
    virtual json execute(const Prompt& prompt, const json& params) = 0;
};

class SchemaInferenceEngine {
public:
    virtual ~SchemaInferenceEngine() = default;
    virtual Tool inferTool(const std::string& naturalLanguageDescription) = 0;
    virtual Prompt inferPrompt(const std::string& naturalLanguageDescription) = 0;
};

class AgentCore {
public:
    virtual ~AgentCore() = default;
    virtual bool init(const std::string& skillsDir) = 0;
    virtual json processGoal(const std::string& goal) = 0;
    virtual bool resumeSession(const std::string& sessionId) = 0;
    virtual std::string currentSessionId() const = 0;
    virtual void run() = 0;
};

// === Docker Integration Interfaces ===

class ContainerManager {
public:
    virtual ~ContainerManager() = default;
    virtual std::string acquireContainer(const Tool& tool) = 0;
    virtual std::string execInContainer(const std::string& containerId,
                                        const std::string& command,
                                        const std::string& stdinData = "") = 0;
    virtual void pruneIdleContainers() = 0;
};

class ComposeManager {
public:
    virtual ~ComposeManager() = default;
    virtual std::string startEnvironment(const Prompt& prompt, const std::string& skillDirectory) = 0;
    virtual void stopEnvironment(const Prompt& prompt) = 0;
    virtual void markUsed(const Prompt& prompt) = 0;
    virtual void setCurrentPrompt(const Prompt& prompt) = 0;
    virtual std::string getCurrentNetwork() const = 0;
    virtual void clearCurrentPrompt() = 0;
};

class DockerToolRunner : public ToolRunner {
public:
    virtual ~DockerToolRunner() = default;
};
