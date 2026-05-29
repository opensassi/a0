#include "docker/docker_tool_runner.h"
#include "docker/container_manager.h"
#include "docker/compose_manager.h"
#include "docker/docker_cli_wrapper.h"
#include "trace.h"
#include <algorithm>
#include <csignal>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>

namespace a0 {
namespace docker {

namespace {

volatile sig_atomic_t s_timeoutFlag = 0;
pid_t s_childPid = 0;

void ephemAlarmHandler(int) {
    s_timeoutFlag = 1;
    if (s_childPid > 0) {
        kill(-s_childPid, SIGKILL);
    }
}

} // anonymous namespace

static std::string shellEscape(const std::string& s) {
    std::string escaped;
    escaped.reserve(s.size() + 2);
    escaped.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped.push_back(c);
        }
    }
    escaped.push_back('\'');
    return escaped;
}

static std::string execDockerRun(const std::string& image,
                                  const std::string& command,
                                  const std::string& stdinData,
                                  int timeoutSecs) {
    std::string cmd = "docker run --rm -i " + image + " sh -c " +
                      shellEscape(command);

    int stdoutPipe[2];
    int stdinPipe[2];
    bool hasStdin = !stdinData.empty();

    if (pipe(stdoutPipe) < 0) return "ERROR: pipe failed";
    if (hasStdin && pipe(stdinPipe) < 0) {
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        return "ERROR: pipe failed";
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        if (hasStdin) { close(stdinPipe[0]); close(stdinPipe[1]); }
        return "ERROR: fork failed";
    }

    if (pid == 0) {
        close(stdoutPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdoutPipe[1]);
        if (hasStdin) {
            close(stdinPipe[1]);
            dup2(stdinPipe[0], STDIN_FILENO);
            close(stdinPipe[0]);
        }
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)NULL);
        _exit(127);
    }

    close(stdoutPipe[1]);
    if (hasStdin) {
        close(stdinPipe[0]);
        const char* data = stdinData.c_str();
        size_t remaining = stdinData.size();
        while (remaining > 0) {
            ssize_t written = write(stdinPipe[1], data, remaining);
            if (written <= 0) break;
            data += written;
            remaining -= written;
        }
        close(stdinPipe[1]);
    }

    s_childPid = pid;
    s_timeoutFlag = 0;
    struct sigaction sa;
    sa.sa_handler = ephemAlarmHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, nullptr);
    alarm(timeoutSecs);

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(stdoutPipe[0], buf, sizeof(buf))) > 0) {
        result.append(buf, n);
    }
    close(stdoutPipe[0]);

    alarm(0);

    int status;
    waitpid(pid, &status, 0);
    s_childPid = 0;

    if (s_timeoutFlag) {
        return "ERROR: timeout";
    }

    return result;
}

DockerToolRunnerImpl::DockerToolRunnerImpl(
    ContainerManager* containerManager,
    ComposeManager* composeManager)
    : m_containerManager(containerManager)
    , m_composeManager(composeManager) {}

std::string DockerToolRunnerImpl::buildCommand(
    const Tool& tool,
    const json& params,
    std::string& outStdin) const {

    if (tool.inputMode == "args") {
        std::string cmd = tool.command;
        if (params.is_object()) {
            for (auto& [key, value] : params.items()) {
                if (key == "_") {
                    if (value.is_string()) {
                        cmd += " " + shellEscape(value.get<std::string>());
                    } else {
                        cmd += " " + shellEscape(value.dump());
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
                    cmd += " --" + key + "=" + shellEscape(val);
                }
            }
        } else if (params.is_string()) {
            cmd += " " + shellEscape(params.get<std::string>());
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
    const std::string& stdinData) const {

    std::string image = tool.dockerImage.empty() ? "ubuntu:22.04" : tool.dockerImage;

    std::string networkFlag;
    if (m_composeManager) {
        std::string network = m_composeManager->getCurrentNetwork();
        if (!network.empty()) {
            networkFlag = " --network=" + network;
        }
    }

    std::string dockerCmd = "docker run --rm -i" + networkFlag + " " + image +
                            " sh -c " + shellEscape(command);
    return execDockerRun(image, command, stdinData, 30);
}

json DockerToolRunnerImpl::run(const Tool& tool, const json& params) {
    TRACE_LOG("DockerToolRunnerImpl::run(" << tool.name << ")");

    std::string stdinData;
    std::string command = buildCommand(tool, params, stdinData);

    if (tool.useContainerPool) {
        std::string containerId = m_containerManager->acquireContainer(tool);

        std::string networkFlag;
        if (m_composeManager) {
            std::string network = m_composeManager->getCurrentNetwork();
            if (!network.empty()) {
                // Attach container to compose network
                std::string connectCmd = "docker network connect " + network +
                                         " " + containerId + " 2>/dev/null";
                // Best-effort: ignore errors if already connected
                (void)connectCmd;
            }
        }

        std::string output = m_containerManager->execInContainer(
            containerId, command, stdinData);
        return output;
    }

    return runEphemeral(tool, command, stdinData);
}

} // namespace docker
} // namespace a0