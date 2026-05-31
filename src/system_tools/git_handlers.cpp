#include "system_tools/registry.h"
#include "command_runner.h"
#include "docker_security_filter.h"
#include <algorithm>
#include <sstream>

using a0::CommandRunner;

namespace a0 {

// ---------------------------------------------------------------------------
// Shell escape for building command strings
// ---------------------------------------------------------------------------

static std::string shellEscape(const std::string& s) {
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    result += '\'';
    return result;
}

// ---------------------------------------------------------------------------
// Build a command from structured params: --flag=value, --flag for booleans
// ---------------------------------------------------------------------------

static std::string buildCommand(const std::string& baseCmd, const json& params) {
    std::string cmd = baseCmd;

    auto argsIt = params.find("args");
    if (argsIt != params.end() && argsIt->is_array()) {
        for (const auto& arg : *argsIt) {
            if (arg.is_string()) cmd += " " + shellEscape(arg.get<std::string>());
            else cmd += " " + arg.dump();
        }
    }

    for (const auto& [key, val] : params.items()) {
        if (key == "args" || key == "timeout") continue;
        std::string flag = key;
        std::replace(flag.begin(), flag.end(), '_', '-');

        if (val.is_boolean() && val.get<bool>()) {
            cmd += " --" + flag;
        } else if (val.is_string() && !val.get<std::string>().empty()) {
            cmd += " --" + flag + "=" + shellEscape(val.get<std::string>());
        } else if (val.is_number()) {
            cmd += " --" + flag + "=" + std::to_string(val.get<int>());
        }
    }

    return cmd;
}

// ---------------------------------------------------------------------------
// Run a CLI command with timeout
// ---------------------------------------------------------------------------

static SystemToolResult runCliCommand(const std::string& cmd, const json& params, int defaultTimeout = 60) {
    int timeout = defaultTimeout;
    auto timeoutIt = params.find("timeout");
    if (timeoutIt != params.end() && timeoutIt->is_number()) {
        timeout = std::min(120, timeoutIt->get<int>() / 1000);
    }

    auto result = CommandRunner::run(cmd, "", timeout);

    if (result.timedOut) {
        return {"ERROR: command timed out"};
    }

    const size_t maxSize = 1024 * 1024;
    std::string output = result.stdout;
    if (!result.stderr.empty()) {
        if (!output.empty()) output += "\n";
        output += result.stderr;
    }
    if (output.size() > maxSize) {
        output.resize(maxSize);
        output += "\n... (truncated)";
    }

    return {output};
}

// ---------------------------------------------------------------------------
// xGitCommand — dispatch all git_* calls
// Called from skill template expansion via {{tool:system_git_commit ...}}
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xGitCommand(const std::string& subcommand, const json& params) {
    std::string cmd = buildCommand("git " + subcommand, params);
    return runCliCommand(cmd, params);
}

// ---------------------------------------------------------------------------
// xDockerCommand — dispatch all docker_* calls
// Checks security filter before destructive operations.
// Called from {{tool:system_docker_run ...}} etc.
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xDockerCommand(const std::string& subcommand, const json& params) {
    // Destructive commands require security filter check
    static const std::vector<std::string> destructive = {
        "rm", "kill", "stop", "pause", "unpause", "rename", "update", "wait"
    };
    bool isDestructive = false;
    for (const auto& d : destructive) {
        if (subcommand == d) { isDestructive = true; break; }
    }

    if (isDestructive && m_dockerSecurityFilter) {
        // Check container parameter against protected list
        auto containerIt = params.find("container");
        if (containerIt != params.end() && containerIt->is_string()) {
            if (!m_dockerSecurityFilter->canAccess(containerIt->get<std::string>())) {
                return {"ERROR: container '" + containerIt->get<std::string>()
                        + "' is a system-managed sandbox container and cannot be modified via user-facing tools."};
            }
        }
        // Also check args array for container references
        auto argsIt = params.find("args");
        if (argsIt != params.end() && argsIt->is_array() && !argsIt->empty()) {
            std::vector<std::string> args;
            for (const auto& a : *argsIt) {
                if (a.is_string()) args.push_back(a.get<std::string>());
            }
            if (!args.empty() && m_dockerSecurityFilter && !m_dockerSecurityFilter->canAccessAll(args)) {
                return {"ERROR: one or more specified containers are system-managed sandbox containers and cannot be modified."};
            }
        }
    }

    std::string cmd = buildCommand("docker " + subcommand, params);
    return runCliCommand(cmd, params, 120);
}

// ---------------------------------------------------------------------------
// xDockerComposeCommand — dispatch all docker_compose_* calls
// Called from {{tool:system_docker_compose_up ...}} etc.
// ---------------------------------------------------------------------------

SystemToolResult SystemToolRegistry::xDockerComposeCommand(const std::string& subcommand, const json& params) {
    std::string cmd = buildCommand("docker compose " + subcommand, params);
    return runCliCommand(cmd, params, 120);
}

} // namespace a0
