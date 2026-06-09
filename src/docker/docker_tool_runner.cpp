#include "docker/docker_tool_runner.h"
#include "docker/container_manager.h"
#include "docker/compose_manager.h"
#include "docker/docker_cli_wrapper.h"
#include "executor/command_runner.h"
#include "shared/trace.h"
#include <sstream>

namespace a0 {
namespace docker {

using a0::CommandRunner;
using a0::StreamHandle;

static std::string execDockerRun(const std::string& image,
                                   const std::string& command,
                                   const std::string& stdinData,
                                   int timeoutSecs)
{
    std::string cmd = "docker run --rm -i " + image + " sh -c " +
                      CommandRunner::shellEscape(command);
    auto result = CommandRunner::run(cmd, stdinData, timeoutSecs);
    if (result.timedOut) {
        return "ERROR: timeout";
    }
    return result.stdout;
}

static StreamHandle execDockerRunStreaming(const std::string& image,
                                            const std::string& command,
                                            const std::string& stdinData,
                                            int timeoutSecs,
                                            const std::string& networkFlag,
                                            a0::StreamCallback onChunk)
{
    std::string dockerCmd = "docker run --rm -i" + networkFlag + " " + image +
                            " sh -c " + CommandRunner::shellEscape(command);
    auto handle = CommandRunner::runStreaming(dockerCmd, std::move(onChunk), timeoutSecs);
    if (!stdinData.empty()) {
        handle.sendInput(stdinData);
    }
    return handle;
}

DockerToolRunnerImpl::DockerToolRunnerImpl(
    ContainerManager* containerManager,
    ComposeManager* composeManager,
    bool poolEnabled)
    : m_containerManager(containerManager)
    , m_composeManager(composeManager)
    , m_poolEnabled(poolEnabled) {}

std::string DockerToolRunnerImpl::buildCommand(
    const Tool& tool,
    const json& params,
    std::string& outStdin) const
{
    if (tool.inputMode == "args") {
        std::string cmd = tool.command;
        if (params.is_object()) {
            for (auto& [key, value] : params.items()) {
                if (key == "_") {
                    if (value.is_string()) {
                        cmd += " " + CommandRunner::shellEscape(value.get<std::string>());
                    } else {
                        cmd += " " + CommandRunner::shellEscape(value.dump());
                    }
                } else {
                    std::string val;
                    if (value.is_string()) {
                        val = value.get<std::string>();
                    } else if (value.is_number()) {
                        val = std::to_string(value.get<double>());
                    } else if (value.is_boolean()) {
                        val = value.get<bool>() ? "true" : "false";
                    } else {
                        val = value.dump();
                    }
                    cmd += " --" + key + "=" + CommandRunner::shellEscape(val);
                }
            }
        } else if (params.is_string()) {
            cmd += " " + CommandRunner::shellEscape(params.get<std::string>());
        }
        outStdin = "";
        return cmd;
    }

    outStdin.clear();
    if (params.is_object() && params.contains("input") &&
        params["input"].is_string()) {
        outStdin = params["input"].get<std::string>();
    } else if (params.is_string()) {
        outStdin = params.get<std::string>();
    } else if (!params.is_null()) {
        outStdin = params.dump();
    }
    return tool.command;
}

std::string DockerToolRunnerImpl::runEphemeral(
    const Tool& tool,
    const std::string& command,
    const std::string& stdinData) const
{
    std::string image = tool.dockerImage.empty() ? "ubuntu:22.04" : tool.dockerImage;

    std::string networkFlag;
    if (m_composeManager) {
        std::string network = m_composeManager->getCurrentNetwork();
        if (!network.empty()) {
            networkFlag = " --network=" + network;
        }
    }

    std::string dockerCmd = "docker run --rm -i" + networkFlag + " " + image +
                            " sh -c " + CommandRunner::shellEscape(command);
    return execDockerRun(image, command, stdinData, tool.timeoutSecs);
}

json DockerToolRunnerImpl::run(const Tool& tool, const json& params)
{
    TRACE_LOG("DockerToolRunnerImpl::run(" << tool.name << ")");

    std::string stdinData;
    std::string command = buildCommand(tool, params, stdinData);

    if (m_poolEnabled && m_containerManager) {
        std::string containerId = m_containerManager->acquireContainer(tool);
        std::string output = m_containerManager->execInContainer(
            containerId, command, stdinData);
        return output;
    }

    return runEphemeral(tool, command, stdinData);
}

StreamHandle DockerToolRunnerImpl::runStreaming(
    const Tool& tool,
    const json& params,
    a0::StreamCallback onChunk)
{
    std::string stdinData;
    std::string command = buildCommand(tool, params, stdinData);

    std::string image = tool.dockerImage.empty() ? "ubuntu:22.04" : tool.dockerImage;

    std::string networkFlag;
    if (m_composeManager) {
        std::string network = m_composeManager->getCurrentNetwork();
        if (!network.empty()) {
            networkFlag = " --network=" + network;
        }
    }

    if (m_poolEnabled && m_containerManager) {
        // Pooled container: use docker exec with streaming
        std::string containerId = m_containerManager->acquireContainer(tool);
        std::string dockerCmd = "docker exec -i " + containerId +
                                " sh -c " + CommandRunner::shellEscape(command);
        auto handle = CommandRunner::runStreaming(dockerCmd, std::move(onChunk),
                                                    tool.timeoutSecs);
        if (!stdinData.empty()) {
            handle.sendInput(stdinData);
        }
        return handle;
    }

    // Ephemeral: docker run --rm with streaming
    return execDockerRunStreaming(image, command, stdinData,
                                   tool.timeoutSecs, networkFlag,
                                   std::move(onChunk));
}

} // namespace docker
} // namespace a0
