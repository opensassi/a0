#include "skill_runner.h"
#include "base_prompt.h"
#include "trace.h"
#include <regex>
#include <sstream>
#include <iostream>

using a0::SystemToolRegistry;

static ToolRunner* selectRunner(const Tool& tool, ToolRunner* host, DockerToolRunner* docker) {
    if (!tool.dockerImage.empty() && docker) {
        return docker;
    }
    return host;
}

static json runTool(const Tool& tool, const json& params,
                     ToolRunner* hostRunner, DockerToolRunner* dockerRunner,
                     SystemToolRegistry* systemTools) {
    if (systemTools && SystemToolRegistry::isSystemTool(tool.name)) {
        auto result = systemTools->execute(tool.name, params);
        return json(result.output);
    }
    ToolRunner* runner = selectRunner(tool, hostRunner, dockerRunner);
    return runner->run(tool, params);
}

DefaultSkillRunner::DefaultSkillRunner(ToolRunner* toolRunner,
                                        InferenceProvider* provider,
                                        SkillRegistry* registry,
                                        DependencyResolver* depResolver,
                                        SystemToolRegistry* systemTools,
                                        DockerToolRunner* dockerRunner,
                                        ComposeManager* composeMgr)
    : m_toolRunner(toolRunner)
    , m_systemTools(systemTools)
    , m_dockerRunner(dockerRunner)
    , m_composeMgr(composeMgr)
    , m_provider(provider)
    , m_registry(registry)
    , m_depResolver(depResolver)
{
}

void DefaultSkillRunner::xRebuildBasePrompt() {
    m_basePrompt = a0::buildBasePrompt(m_registry);
}

void DefaultSkillRunner::setSkillsDir(const std::string& path) {
    m_skillsDir = path;
}

std::string DefaultSkillRunner::expandPrompt(const Prompt& prompt, const json& params) {
    TRACE_LOG("expandPrompt(" << prompt.name << ")");
    std::string result = prompt.prompt;

    // Pass 1: replace {{key}} simple placeholders
    for (auto it = params.begin(); it != params.end(); ++it) {
        std::string key = "{{" + it.key() + "}}";
        std::string val;
        if (it->is_string()) {
            val = it->get<std::string>();
        } else {
            val = it->dump();
        }

        size_t pos = 0;
        while ((pos = result.find(key, pos)) != std::string::npos) {
            result.replace(pos, key.length(), val);
            pos += val.length();
        }
    }

    // Pass 2: replace {{tool:name key="value" ...}} eager tool calls
    std::regex toolRe(R"(\{\{tool:(\w+)\s+([^}]+)\}\})");
    std::smatch match;
    while (std::regex_search(result, match, toolRe)) {
        std::string toolName = match[1];
        std::string argsStr = match[2];

        // Parse key="value" pairs with auto-type conversion
        json toolParams;
        std::regex argRe(R"((\w+)=["']([^"']*)["'])");
        std::smatch argMatch;
        std::string::const_iterator searchStart = argsStr.cbegin();
        while (std::regex_search(searchStart, argsStr.cend(), argMatch, argRe)) {
            std::string val = argMatch[2];
            // Auto-convert numeric and boolean strings to match opencode params
            if (val == "true") {
                toolParams[argMatch[1]] = true;
            } else if (val == "false") {
                toolParams[argMatch[1]] = false;
            } else if (!val.empty() && val.find_first_not_of("0123456789") == std::string::npos) {
                toolParams[argMatch[1]] = std::stoi(val);
            } else {
                toolParams[argMatch[1]] = val;
            }
            searchStart = argMatch.suffix().first;
        }

        auto toolOpt = m_registry->getTool(toolName);
        if (!toolOpt.has_value()) {
            result.replace(match.position(), match.length(), "ERROR: tool not found: " + toolName);
            continue;
        }

        json toolResult = runTool(*toolOpt, toolParams,
                                   m_toolRunner, m_dockerRunner, m_systemTools);

        std::string replacement;
        if (toolResult.is_string()) {
            replacement = toolResult.get<std::string>();
        } else {
            replacement = toolResult.dump();
        }

        result.replace(match.position(), match.length(), replacement);
    }

    return result;
}

json DefaultSkillRunner::runValidators(const Prompt& prompt, const json& input) {
    TRACE_LOG("runValidators(" << prompt.name << ")");
    json current = input;
    for (const auto& vb : prompt.validators) {
        auto toolOpt = m_registry->getTool(vb.toolName);
        if (!toolOpt.has_value()) {
            current = "VALIDATOR_ERROR: tool not found: " + vb.toolName;
            break;
        }

        json params;
        if (current.is_string()) {
            params["input"] = current.get<std::string>();
        } else {
            params["input"] = current.dump();
        }

        json validatorResult = runTool(*toolOpt, params,
                                        m_toolRunner, m_dockerRunner, m_systemTools);

        if (validatorResult.is_string()) {
            std::string out = validatorResult.get<std::string>();
            if (out.find("ERROR:") == 0) {
                current = "VALIDATOR_ERROR: " + out;
                break;
            }
        }
        current = validatorResult;
    }
    return current;
}

json DefaultSkillRunner::execute(const Prompt& prompt, const json& params) {
    TRACE_LOG("execute(" << prompt.name << ")");
    auto missing = m_depResolver->missingDependencies(prompt);
    if (!missing.empty()) {
        std::string err = "Missing dependencies: ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) err += ", ";
            err += missing[i];
        }
        return json(err);
    }

    // Start compose environment if needed
    if (!prompt.composeFile.empty() && m_composeMgr) {
        m_composeMgr->startEnvironment(prompt, m_skillsDir);
    }

    std::string expanded = expandPrompt(prompt, params);

    // Rebuild base prompt lazily on first execute
    if (m_basePrompt.empty()) {
        xRebuildBasePrompt();
    }

    // Prepend base prompt to skill-specific system description
    std::string fullSystemPrompt = m_basePrompt;
    if (!prompt.description.empty()) {
        if (!fullSystemPrompt.empty()) fullSystemPrompt += "\n\n";
        fullSystemPrompt += prompt.description;
    }

    std::string llmResult = m_provider->complete(fullSystemPrompt, expanded);

    json finalResult = runValidators(prompt, llmResult);

    // Stop compose if needed
    if (!prompt.composeFile.empty() && m_composeMgr) {
        m_composeMgr->stopEnvironment(prompt);
    }

    return finalResult;
}
