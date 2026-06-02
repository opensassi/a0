#include "skill_runner.h"
#include "base_prompt.h"
#include "skills/skills.h"
#include "trace.h"
#include <regex>
#include <sstream>
#include <iostream>

static ToolRunner* selectRunner(const Tool& tool, ToolRunner* host, DockerToolRunner* docker) {
    if (!tool.dockerImage.empty() && docker) {
        return docker;
    }
    return host;
}

static json runTool(const a0::skills::SkillTool& skillTool, const json& params,
                     ToolRunner* hostRunner, DockerToolRunner* dockerRunner) {
    Tool t;
    t.name = skillTool.name;
    t.description = skillTool.description;
    t.command = skillTool.command;
    t.inputMode = skillTool.inputMode;
    t.dockerImage = skillTool.dockerImage;
    t.trustLevel = skillTool.trustLevel;
    t.aptDependencies = skillTool.aptDependencies;
    t.timeoutSecs = skillTool.timeoutSecs;
    ToolRunner* runner = selectRunner(t, hostRunner, dockerRunner);
    return runner->run(t, params);
}

static a0::StreamHandle runToolStreaming(const a0::skills::SkillTool& skillTool,
                                           const json& params,
                                           a0::StreamCallback onChunk,
                                           ToolRunner* hostRunner,
                                           DockerToolRunner* dockerRunner) {
    Tool t;
    t.name = skillTool.name;
    t.description = skillTool.description;
    t.command = skillTool.command;
    t.inputMode = skillTool.inputMode;
    t.dockerImage = skillTool.dockerImage;
    t.trustLevel = skillTool.trustLevel;
    t.aptDependencies = skillTool.aptDependencies;
    t.timeoutSecs = skillTool.timeoutSecs;
    ToolRunner* runner = selectRunner(t, hostRunner, dockerRunner);
    return runner->runStreaming(t, params, std::move(onChunk));
}

DefaultSkillRunner::DefaultSkillRunner(ToolRunner* toolRunner,
                                        InferenceProvider* provider,
                                        a0::skills::SkillManager* skillMgr,
                                        DependencyResolver* depResolver,
                                        DockerToolRunner* dockerRunner,
                                        ComposeManager* composeMgr)
    : m_toolRunner(toolRunner)
    , m_dockerRunner(dockerRunner)
    , m_composeMgr(composeMgr)
    , m_provider(provider)
    , m_skillMgr(skillMgr)
    , m_depResolver(depResolver)
{
}

void DefaultSkillRunner::xRebuildBasePrompt() {
    m_basePrompt = a0::buildBasePrompt(m_skillMgr);
}

void DefaultSkillRunner::setSkillsDir(const std::string& path) {
    m_skillsDir = path;
}

void DefaultSkillRunner::setGlobalVar(const std::string& key, const std::string& value) {
    m_globalVars[key] = value;
}

void DefaultSkillRunner::setGlobalVars(const std::unordered_map<std::string, std::string>& vars) {
    for (const auto& [k, v] : vars) {
        m_globalVars[k] = v;
    }
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

    // Pass 1b: replace {{GLOBAL_KEY}} from global variables
    for (const auto& [key, val] : m_globalVars) {
        std::string placeholder = "{{" + key + "}}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), val);
            pos += val.length();
        }
    }

    // Pass 2: replace {{tool:qualified_name key="value" ...}} eager tool calls
    std::regex toolRe(R"(\{\{tool:([\w:-]+)\s+([^}]+)\}\})");
    std::smatch match;
    while (std::regex_search(result, match, toolRe)) {
        std::string toolName = match[1];
        std::string argsStr = match[2];

        json toolParams;
        std::regex argRe(R"((\w+)=["']([^"']*)["'])");
        std::smatch argMatch;
        std::string::const_iterator searchStart = argsStr.cbegin();
        while (std::regex_search(searchStart, argsStr.cend(), argMatch, argRe)) {
            std::string val = argMatch[2];
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

        a0::skills::SkillTool skillTool;
        std::string resolveName = toolName;
        bool found = false;

        if (m_skillMgr && m_skillMgr->getTool(toolName, skillTool) == 0) {
            found = true;
        }

        if (!found && toolName.find(':') == std::string::npos &&
            !prompt.ns.empty() && !prompt.component.empty()) {
            resolveName = a0::skills::buildQualifiedName(prompt.ns, prompt.component, toolName);
            if (m_skillMgr && m_skillMgr->getTool(resolveName, skillTool) == 0) {
                found = true;
            }
        }

        if (!found) {
            result.replace(match.position(), match.length(), "ERROR: tool not found: " + toolName);
            continue;
        }

        json toolResult;
        if (skillTool.systemTool && m_skillMgr) {
            auto hr = m_skillMgr->executeToolWithMeta(resolveName, toolParams);
            toolResult = hr.output;
        } else {
            toolResult = runTool(skillTool, toolParams,
                                  m_toolRunner, m_dockerRunner);
        }

        std::string replacement;
        if (toolResult.is_string()) {
            replacement = toolResult.get<std::string>();
        } else {
            replacement = toolResult.dump();
        }
        result.replace(match.position(), match.length(), replacement);
    }

    std::regex toolCallRe(R"(\{\{tool_call:([\w:-]+)\s*([^}]*)\}\})");
    std::string pass3;
    std::string::const_iterator pos3 = result.cbegin();
    std::smatch tcMatch;
    while (std::regex_search(pos3, result.cend(), tcMatch, toolCallRe)) {
        pass3.append(pos3, tcMatch[0].first);
        std::string qName = tcMatch[1];
        auto lastColon = qName.find_last_of(':');
        std::string shortName = (lastColon != std::string::npos) ? qName.substr(lastColon + 1) : qName;
        pass3 += shortName;
        pos3 = tcMatch[0].second;
    }
    pass3.append(pos3, result.cend());
    result = pass3;

    return result;
}

json DefaultSkillRunner::runValidators(const Prompt& prompt, const json& input) {
    TRACE_LOG("runValidators(" << prompt.name << ")");
    json current = input;
    for (const auto& vb : prompt.validators) {
        a0::skills::SkillTool skillTool;
        bool found = false;
        if (m_skillMgr && m_skillMgr->getTool(vb.toolName, skillTool) == 0) {
            found = true;
        }

        if (!found) {
            current = "VALIDATOR_ERROR: tool not found: " + vb.toolName;
            break;
        }

        json params;
        if (current.is_string()) {
            params["input"] = current.get<std::string>();
        } else {
            params["input"] = current.dump();
        }

        json validatorResult = runTool(skillTool, params,
                                        m_toolRunner, m_dockerRunner);

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

a0::StreamHandle DefaultSkillRunner::executeStreaming(
    const Prompt& prompt, const json& params, a0::StreamCallback onChunk)
{
    TRACE_LOG("executeStreaming(" << prompt.name << ")");
    auto missing = m_depResolver->missingDependencies(prompt);
    if (!missing.empty()) {
        // Return a no-op handle (already done)
        a0::StreamHandle h;
        return h;
    }

    if (!prompt.composeFile.empty() && m_composeMgr) {
        m_composeMgr->startEnvironment(prompt, m_skillsDir);
    }

    // Check if params specify a direct tool invocation
    std::string toolName = params.value("_tool", "");
    bool streaming = params.value("streaming", false);

    if (!toolName.empty() && streaming) {
        a0::skills::SkillTool skillTool;
        if (m_skillMgr && m_skillMgr->getTool(toolName, skillTool) == 0) {
            json toolParams = params;
            toolParams.erase("_tool");
            toolParams.erase("streaming");
            return runToolStreaming(skillTool, toolParams,
                                     std::move(onChunk),
                                     m_toolRunner, m_dockerRunner);
        }
    }

    // Default: no streaming tool found, return empty handle
    a0::StreamHandle h;
    return h;
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
