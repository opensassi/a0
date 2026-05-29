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
                                        ComponentRegistry* registry,
                                        DependencyResolver* depResolver,
                                        DockerToolRunner* dockerRunner,
                                        ComposeManager* composeMgr)
    : m_toolRunner(toolRunner)
    , m_dockerRunner(dockerRunner)
    , m_composeMgr(composeMgr)
    , m_provider(provider)
    , m_registry(registry)
    , m_depResolver(depResolver) {}

void DefaultSkillRunner::setComponentsDir(const std::string& path) {
    m_componentsDir = path;
}

static ToolRunner* selectRunner(const Tool& tool,
                                 ToolRunner* host,
                                 DockerToolRunner* docker) {
    if (!tool.dockerImage.empty() && docker) {
        return docker;
    }
    return host;
}

std::string DefaultSkillRunner::expandPrompt(const Skill& skill, const json& params) {
    TRACE_LOG("expandPrompt(" << skill.name << ")");
    std::string result = skill.prompt;
    std::regex simplePlaceholder(R"(\{\{([a-zA-Z_][a-zA-Z0-9_]*)\}\})");
    std::string substituted;
    auto begin = std::sregex_iterator(result.begin(), result.end(), simplePlaceholder);
    auto end = std::sregex_iterator();
    size_t lastPos = 0;
    for (auto it = begin; it != end; ++it) {
        substituted += result.substr(lastPos, it->position() - lastPos);
        std::string key = (*it)[1].str();
        if (params.contains(key)) {
            const json& value = params[key];
            if (value.is_string()) {
                substituted += value.get<std::string>();
            } else {
                substituted += value.dump();
            }
        } else {
            substituted += it->str();
        }
        lastPos = it->position() + it->length();
    }
    substituted += result.substr(lastPos);
    result = substituted;
    std::regex placeholderRe(R"(\{\{tool:(\w+)([^}]*)\}\})");
    std::string output;
    auto toolBegin = std::sregex_iterator(result.begin(), result.end(), placeholderRe);
    auto toolEnd = std::sregex_iterator();
    size_t toolLastPos = 0;

    for (auto it = toolBegin; it != toolEnd; ++it) {
        output += result.substr(toolLastPos, it->position() - toolLastPos);
        std::string toolName = (*it)[1].str();
        std::string argsStr = (*it)[2].str();
        json toolArgs = parsePlaceholderArgs(argsStr);

        auto toolOpt = m_registry->getTool(toolName);
        if (toolOpt.has_value()) {
            ToolRunner* runner = selectRunner(*toolOpt, m_toolRunner, m_dockerRunner);
            json toolResult = runner->run(*toolOpt, toolArgs);
            if (toolResult.is_string()) {
                output += toolResult.get<std::string>();
            } else {
                output += toolResult.dump();
            }
        } else {
            output += it->str();
        }

        toolLastPos = it->position() + it->length();
    }
    output += result.substr(toolLastPos);
    TRACE_LOG("expandPrompt result=" << output);
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
        ToolRunner* runner = selectRunner(*toolOpt, m_toolRunner, m_dockerRunner);
        current = runner->run(*toolOpt, params);
        if (current.is_string()) {
            std::string strResult = current.get<std::string>();
            if (strResult.rfind("ERROR:", 0) == 0) {
                return "VALIDATOR_ERROR: " + strResult;
            }
        }
    }
    return current;
}

json DefaultSkillRunner::execute(const Skill& skill, const json& params) {
    TRACE_LOG("execute(" << skill.name << ")");
    if (m_depResolver) {
        auto missing = m_depResolver->missingDependencies(skill);
        if (!missing.empty()) {
            std::string err = "Missing dependencies: ";
            for (size_t i = 0; i < missing.size(); ++i) {
                if (i > 0) err += ", ";
                err += missing[i];
            }
            return err;
        }
    }

    bool hasCompose = !skill.composeFile.empty() && m_composeMgr;
    if (hasCompose) {
        m_composeMgr->startEnvironment(skill, m_componentsDir);
        m_composeMgr->setCurrentSkill(skill);
    }

    std::string expanded = expandPrompt(skill, params);
    std::string llmResponse = m_provider->complete(skill.description, expanded);
    json result = llmResponse;
    if (!skill.validators.empty()) {
        result = runValidators(skill, result);
    }

    if (hasCompose) {
        m_composeMgr->clearCurrentSkill();
        m_composeMgr->markUsed(skill);
    }

    return result;
}
