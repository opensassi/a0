#include "docker/docker_cli_wrapper.h"
#include "../command_runner.h"
#include "trace.h"
#include <stdexcept>

namespace a0 {
namespace docker {

using a0::CommandRunner;

static std::string execSimple(const std::string& cmd)
{
    TRACE_LOG("docker simple: " << cmd);
    auto result = CommandRunner::run(cmd, "", 120);
    if (result.exitCode != 0) {
        throw std::runtime_error("docker command failed (exit " +
                                 std::to_string(result.exitCode) + "): " + cmd);
    }
    // Trim trailing newlines
    while (!result.stdout.empty() &&
           (result.stdout.back() == '\n' || result.stdout.back() == '\r')) {
        result.stdout.pop_back();
    }
    return result.stdout;
}

std::string DockerCLIWrapper::runDetached(const std::string& image,
                                           const std::string& name,
                                           const std::string& command)
{
    std::string cmd = "docker run -d --name " + name + " " + image +
                      " sh -c " + CommandRunner::shellEscape(command);
    return execSimple(cmd);
}

std::string DockerCLIWrapper::execInContainer(const std::string& containerId,
                                               const std::string& command,
                                               const std::string& stdinData,
                                               int timeoutSecs)
{
    std::string cmd = "docker exec -i " + containerId + " sh -c " +
                      CommandRunner::shellEscape(command);
    auto result = CommandRunner::run(cmd, stdinData, timeoutSecs);
    if (result.timedOut) {
        throw std::runtime_error("timeout");
    }
    if (result.exitCode != 0 && result.stdout.empty()) {
        throw std::runtime_error("command failed with exit code " +
                                 std::to_string(result.exitCode));
    }
    return result.stdout;
}

void DockerCLIWrapper::stopAndRemove(const std::string& containerId)
{
    try { execSimple("docker stop " + containerId + " 2>/dev/null"); } catch (...) {}
    try { execSimple("docker rm " + containerId + " 2>/dev/null"); } catch (...) {}
}

std::string DockerCLIWrapper::getContainerId(const std::string& name) {
    try {
        std::string cmd = "docker ps -aq --filter name=^/" + name + "$";
        std::string result = execSimple(cmd);
        return result;
    } catch (...) {
        return "";
    }
}

void DockerCLIWrapper::startContainer(const std::string& name) {
    try {
        execSimple("docker start " + name + " 2>/dev/null");
    } catch (...) {}
}

void DockerCLIWrapper::pullImage(const std::string& image)
{
    execSimple("docker pull " + image);
}

void DockerCLIWrapper::composeUp(const std::string& composeFile,
                                  const std::string& projectDir)
{
    std::string cmd = "docker-compose -f " + composeFile + " -p " +
                      projectDir + " up -d";
    execSimple(cmd);
}

void DockerCLIWrapper::composeDown(const std::string& composeFile,
                                    const std::string& projectDir)
{
    std::string cmd = "docker-compose -f " + composeFile + " -p " +
                      projectDir + " down";
    execSimple(cmd);
}

std::string DockerCLIWrapper::getNetworkName(const std::string& composeFile,
                                              const std::string& projectDir)
{
    size_t pos = projectDir.find_last_of('/');
    std::string dirName = (pos == std::string::npos)
                              ? projectDir
                              : projectDir.substr(pos + 1);
    return dirName + "_default";
}

} // namespace docker
} // namespace a0
