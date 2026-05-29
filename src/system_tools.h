#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ToolSchema;

namespace a0 {

struct SystemToolResult {
    std::string output;
};

class SystemToolRegistry {
public:
    SystemToolRegistry();

    SystemToolResult execute(const std::string& toolName, const json& params);

    static bool isSystemTool(const std::string& name);

    std::vector<std::string> listTools() const;

    /// Build ToolSchema array for LLM function calling
    std::vector<ToolSchema> schemas() const;

private:
    using Handler = std::function<SystemToolResult(const json&)>;

    static SystemToolResult xBash(const json& params);
    static SystemToolResult xRead(const json& params);
    static SystemToolResult xGlob(const json& params);
    static SystemToolResult xGrep(const json& params);
    static SystemToolResult xEdit(const json& params);
    static SystemToolResult xWrite(const json& params);

    std::unordered_map<std::string, Handler> m_handlers;
};

} // namespace a0
