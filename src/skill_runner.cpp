#include "skill_runner.h"
#include "trace.h"
#include <regex>
#include <sstream>

static json parsePlaceholderArgs(const std::string& argsStr) {
    json args = json::object();
    std::regex argRe(R"((\w+)=["']([^"']*)["'])");
    auto begin = std::sregex_iterator(argsStr.begin(), argsStr.end(), argRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        args[(*it)[1].str()] = (*it)[2].str();
    }
    return args;
}

DefaultSkillRunner::DefaultSkillRunner(ToolRunner* toolRunner,
                                       InferenceProvider* provider,
                                       ComponentRegistry* registry)
    : m_toolRunner(toolRunner)
    , m_provider(provider)
    , m_registry(registry) {}

std::string DefaultSkillRunner::expandPrompt(const Skill& skill, const json& params) {
    TRACE_LOG("expandPrompt(" << skill.name << ")");
    (void)params;
    std::string result = skill.prompt;
    std::regex placeholderRe(R"(\{\{tool:(\w+)([^}]*)\}\})");
    std::string output;
    auto begin = std::sregex_iterator(result.begin(), result.end(), placeholderRe);
    auto end = std::sregex_iterator();
    size_t lastPos = 0;

    for (auto it = begin; it != end; ++it) {
        output += result.substr(lastPos, it->position() - lastPos);
        std::string toolName = (*it)[1].str();
        std::string argsStr = (*it)[2].str();
        json toolArgs = parsePlaceholderArgs(argsStr);

        auto toolOpt = m_registry->getTool(toolName);
        if (toolOpt.has_value()) {
            json toolResult = m_toolRunner->run(*toolOpt, toolArgs);
            if (toolResult.is_string()) {
                output += toolResult.get<std::string>();
            } else {
                output += toolResult.dump();
            }
        } else {
            output += it->str();
        }

        lastPos = it->position() + it->length();
    }
    output += result.substr(lastPos);
    return output;
}

json DefaultSkillRunner::runValidators(const Skill& skill, const json& input) {
    TRACE_LOG("runValidators(" << skill.name << ")");
    json current = input;
    for (const auto& vb : skill.validators) {
        auto toolOpt = m_registry->getTool(vb.toolName);
        if (!toolOpt.has_value()) continue;

        json params;
        if (current.is_string()) {
            params["input"] = current.get<std::string>();
        } else {
            params["input"] = current.dump();
        }
        current = m_toolRunner->run(*toolOpt, params);
    }
    return current;
}

json DefaultSkillRunner::execute(const Skill& skill, const json& params) {
    TRACE_LOG("execute(" << skill.name << ")");
    std::string expanded = expandPrompt(skill, params);
    std::string llmResponse = m_provider->complete(skill.description, expanded);
    json result = llmResponse;
    if (!skill.validators.empty()) {
        result = runValidators(skill, result);
    }
    return result;
}
