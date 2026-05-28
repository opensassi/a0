#include "tool_runner.h"
#include "trace.h"
#include <algorithm>
#include <csignal>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>

static volatile sig_atomic_t g_timeoutFlag = 0;
static pid_t g_childPid = 0;

extern "C" void handleAlarm(int) {
    g_timeoutFlag = 1;
    if (g_childPid > 0) {
        kill(-g_childPid, SIGKILL);
    }
}

static std::string shellEscape(const std::string& s) {
    TRACE_LOG("shellEscape(len=" << s.size() << ")");
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

static std::string exec(const std::string& cmd, const std::string& stdinData) {
    TRACE_LOG("exec(" << cmd << ")");

    int stdoutPipe[2];
    int stdinPipe[2];
    bool hasStdin = !stdinData.empty();

    if (pipe(stdoutPipe) < 0) return "ERROR: pipe failed";
    if (hasStdin && pipe(stdinPipe) < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return "ERROR: pipe failed";
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
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

    g_childPid = pid;
    g_timeoutFlag = 0;
    struct sigaction sa;
    sa.sa_handler = handleAlarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, nullptr);
    alarm(30);

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
    g_childPid = 0;

    if (g_timeoutFlag) {
        return "ERROR: timeout";
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        if (result.empty()) {
            return "ERROR: command failed with exit code " + std::to_string(WEXITSTATUS(status));
        }
    }

    const size_t maxSize = 1024 * 1024;
    if (result.size() > maxSize) {
        result.resize(maxSize);
        result += "... (truncated)";
    }

    return result;
}

json SubprocessToolRunner::run(const Tool& tool, const json& params) {
    TRACE_LOG("run(" << tool.name << ")");

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
        std::string output = exec(cmd, "");
        return output;
    }

    std::string input;
    if (params.is_object() && params.contains("input") && params["input"].is_string()) {
        input = params["input"].get<std::string>();
    } else if (params.is_string()) {
        input = params.get<std::string>();
    } else {
        input = params.dump();
    }

    std::string output = exec(tool.command, input);
    return output;
}
