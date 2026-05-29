#include "docker/docker_cli_wrapper.h"
#include "trace.h"
#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>

namespace a0 {
namespace docker {

namespace {

volatile sig_atomic_t s_timeoutFlag = 0;
pid_t s_childPid = 0;

void dockerAlarmHandler(int) {
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

static std::string execSimple(const std::string& cmd) {
    TRACE_LOG("docker simple: " << cmd);
    std::array<char, 4096> buffer;
    std::string result;
    FILE* fp = popen((cmd + " 2>&1").c_str(), "r");
    if (!fp) {
        throw std::runtime_error("popen failed: " + cmd);
    }
    while (fgets(buffer.data(), buffer.size(), fp) != nullptr) {
        result += buffer.data();
    }
    int rc = pclose(fp);
    if (rc != 0) {
        int exitCode = WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
        throw std::runtime_error("docker command failed (exit " +
                                 std::to_string(exitCode) + "): " + cmd);
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
        result.pop_back();
    }
    return result;
}

static std::string execWithTimeout(const std::string& cmd,
                                    const std::string& stdinData,
                                    int timeoutSecs) {
    TRACE_LOG("docker exec: " << cmd);

    int stdoutPipe[2];
    int stdinPipe[2];
    bool hasStdin = !stdinData.empty();

    if (pipe(stdoutPipe) < 0) {
        throw std::runtime_error("pipe failed");
    }
    if (hasStdin && pipe(stdinPipe) < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        throw std::runtime_error("pipe failed");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        if (hasStdin) {
            close(stdinPipe[0]);
            close(stdinPipe[1]);
        }
        throw std::runtime_error("fork failed");
    }

    if (pid == 0) {
        close(stdoutPipe[0]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        close(stdoutPipe[1]);
        if (hasStdin) {
            close(stdinPipe[1]);
            dup2(stdinPipe[0], STDIN_FILENO);
            close(stdinPipe[0]);
        } else {
            close(STDIN_FILENO);
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
    sa.sa_handler = dockerAlarmHandler;
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
        throw std::runtime_error("timeout");
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        if (result.empty()) {
            throw std::runtime_error("command failed with exit code " +
                                     std::to_string(WEXITSTATUS(status)));
        }
    }

    return result;
}

std::string DockerCLIWrapper::runDetached(const std::string& image,
                                           const std::string& name,
                                           const std::string& command) {
    std::string cmd = "docker run -d --name " + name + " " + image +
                      " sh -c " + shellEscape(command);
    return execSimple(cmd);
}

std::string DockerCLIWrapper::execInContainer(const std::string& containerId,
                                               const std::string& command,
                                               const std::string& stdinData,
                                               int timeoutSecs) {
    std::string cmd = "docker exec -i " + containerId + " sh -c " +
                      shellEscape(command);
    return execWithTimeout(cmd, stdinData, timeoutSecs);
}

void DockerCLIWrapper::stopAndRemove(const std::string& containerId) {
    try {
        execSimple("docker stop " + containerId + " 2>/dev/null");
    } catch (...) {}
    try {
        execSimple("docker rm " + containerId + " 2>/dev/null");
    } catch (...) {}
}

void DockerCLIWrapper::pullImage(const std::string& image) {
    execSimple("docker pull " + image);
}

void DockerCLIWrapper::composeUp(const std::string& composeFile,
                                  const std::string& projectDir) {
    std::string cmd = "docker-compose -f " + composeFile + " -p " +
                      projectDir + " up -d";
    execSimple(cmd);
}

void DockerCLIWrapper::composeDown(const std::string& composeFile,
                                    const std::string& projectDir) {
    std::string cmd = "docker-compose -f " + composeFile + " -p " +
                      projectDir + " down";
    execSimple(cmd);
}

std::string DockerCLIWrapper::getNetworkName(const std::string& composeFile,
                                              const std::string& projectDir) {
    // Docker Compose default network name is <project>_default
    // where project is the basename of the project directory
    size_t pos = projectDir.find_last_of('/');
    std::string dirName = (pos == std::string::npos)
                              ? projectDir
                              : projectDir.substr(pos + 1);
    return dirName + "_default";
}

} // namespace docker
} // namespace a0