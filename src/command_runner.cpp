#include "command_runner.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>

namespace a0 {

static volatile sig_atomic_t g_timeoutFired = 0;

namespace {

void alarmHandler(int)
{
    g_timeoutFired = 1;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

CommandResult CommandRunner::run(const std::string& cmd,
                                  const std::string& stdinData,
                                  int timeoutSecs)
{
    return xRunSingle(cmd, stdinData, timeoutSecs);
}

std::vector<CommandResult> CommandRunner::runAll(
    const std::vector<std::string>& cmds,
    int timeoutSecs,
    int maxParallel)
{
    if (maxParallel == 0) {
        maxParallel = static_cast<int>(cmds.size());
    }
    std::vector<CommandResult> results(cmds.size());
    std::vector<pid_t> children;

    size_t next = 0;
    size_t started = 0;

    while (started < cmds.size()) {
        // Launch up to maxParallel children
        while (children.size() < static_cast<size_t>(maxParallel) && next < cmds.size()) {
            // Create pipes
            int stdoutPipe[2], stderrPipe[2];
            if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
                CommandResult err;
                err.exitCode = -1;
                err.stderr = "pipe failed";
                results[next] = err;
                next++;
                started++;
                continue;
            }

            pid_t pid = fork();
            if (pid == 0) {
                // Child
                close(stdoutPipe[0]);
                close(stderrPipe[0]);
                dup2(stdoutPipe[1], STDOUT_FILENO);
                dup2(stderrPipe[1], STDERR_FILENO);
                if (stdoutPipe[1] != STDOUT_FILENO) close(stdoutPipe[1]);
                if (stderrPipe[1] != STDERR_FILENO) close(stderrPipe[1]);
                setpgid(0, 0);
                execl("/bin/sh", "sh", "-c", cmds[next].c_str(), nullptr);
                _exit(127);
            } else if (pid > 0) {
                // Parent
                close(stdoutPipe[1]);
                close(stderrPipe[1]);
                children.push_back(pid);
                // Store pipe fds for reading
                results[next].exitCode = stdoutPipe[0];   // hack: store fd temporarily
                results[next].timedOut = false;
                next++;
                started++;
            } else {
                CommandResult err;
                err.exitCode = -1;
                err.stderr = "fork failed";
                results[next] = err;
                next++;
                started++;
            }
        }

        // Wait for all children in this batch
        for (auto& pid : children) {
            int status;
            waitpid(pid, &status, 0);
        }
        children.clear();
    }

    // TODO: proper parallel reading with non-blocking I/O
    // For now, runAll is a serial fallback
    results.clear();
    for (const auto& cmd : cmds) {
        results.push_back(xRunSingle(cmd, "", timeoutSecs));
    }
    return results;
}

// ---------------------------------------------------------------------------
// StreamHandle methods
// ---------------------------------------------------------------------------

bool StreamHandle::isDone() const
{
    return m_state && m_state->done.load();
}

int StreamHandle::wait()
{
    if (!m_state) return -1;
    if (m_state->thread.joinable()) {
        m_state->thread.join();
    }
    return m_state->exitCode.load();
}

void StreamHandle::cancel()
{
    if (!m_state || m_state->done.load()) return;
    pid_t pgid = m_state->childPid;
    if (pgid > 0) {
        kill(-pgid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        kill(-pgid, SIGKILL);
    }
}

void StreamHandle::sendInput(const std::string& data)
{
    if (!m_state || m_state->done.load()) return;
    std::lock_guard<std::mutex> lock(m_state->mutex);
    if (m_state->stdinFd >= 0) {
        write(m_state->stdinFd, data.data(), data.size());
    }
}

// ---------------------------------------------------------------------------
// Streaming implementation
// ---------------------------------------------------------------------------

StreamHandle CommandRunner::runStreaming(const std::string& cmd,
                                          StreamCallback onChunk,
                                          int timeoutSecs)
{
    StreamHandle handle;
    auto state = std::make_shared<StreamHandle::State>();
    handle.m_state = state;

    int stdoutPipe[2], stderrPipe[2], stdinPipe[2];
    if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0 || pipe(stdinPipe) != 0) {
        state->done = true;
        return handle;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        close(stderrPipe[0]); close(stderrPipe[1]);
        close(stdinPipe[0]); close(stdinPipe[1]);
        state->done = true;
        return handle;
    }

    if (pid == 0) {
        // Child
        close(stdoutPipe[0]);
        close(stderrPipe[0]);
        close(stdinPipe[1]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        dup2(stdinPipe[0], STDIN_FILENO);
        close(stdoutPipe[1]);
        close(stderrPipe[1]);
        close(stdinPipe[0]);
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    // Parent
    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    close(stdinPipe[0]);

    handle.pid = pid;
    state->childPid = pid;
    state->stdinFd = stdinPipe[1];

    // Reader thread: reads stdout and stderr, calls callback
    state->thread = std::thread([state, stdoutPipe0 = stdoutPipe[0],
                                  stderrPipe0 = stderrPipe[0],
                                  stdinPipe1 = stdinPipe[1],
                                  onChunk = std::move(onChunk),
                                  timeoutSecs, pid]() {
        // Set alarm for timeout in this thread
        if (timeoutSecs > 0) {
            alarm(static_cast<unsigned int>(timeoutSecs));
        }

        // Read stdout and stderr using poll
        int outFd = stdoutPipe0;
        int errFd = stderrPipe0;
        bool outDone = false, errDone = false;
        char buf[4096];

        while (!outDone || !errDone) {
            struct pollfd fds[2];
            nfds_t nfds = 0;
            if (!outDone) { fds[nfds].fd = outFd; fds[nfds].events = POLLIN; nfds++; }
            if (!errDone) { fds[nfds].fd = errFd; fds[nfds].events = POLLIN; nfds++; }

            int ret = poll(fds, nfds, 100);  // 100ms timeout for checking done flag

            if (ret < 0) break;

            if (ret > 0) {
                for (nfds_t i = 0; i < nfds; i++) {
                    if (fds[i].revents & POLLIN) {
                        int fd = fds[i].fd;
                        ssize_t n = read(fd, buf, sizeof(buf));
                        if (n > 0) {
                            std::string dir = (fd == outFd) ? "stdout" : "stderr";
                            std::string chunk(buf, static_cast<size_t>(n));
                            if (onChunk) onChunk(chunk, dir);
                        } else {
                            if (fd == outFd) outDone = true;
                            else errDone = true;
                        }
                    } else if (fds[i].revents & (POLLHUP | POLLERR)) {
                        if (fds[i].fd == outFd) outDone = true;
                        else errDone = true;
                    }
                }
            }
        }

        alarm(0);
        close(outFd);
        close(errFd);
        close(stdinPipe1);

        // Wait for child
        int status;
        waitpid(pid, &status, 0);

        int ec = -1;
        if (WIFEXITED(status)) {
            ec = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            ec = -WTERMSIG(status);
        }
        state->exitCode = ec;
        state->done = true;
    });

    return handle;
}

std::string CommandRunner::shellEscape(const std::string& s)
{
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";
        } else {
            result += c;
        }
    }
    result += '\'';
    return result;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

CommandResult CommandRunner::xRunSingle(const std::string& cmd,
                                         const std::string& stdinData,
                                         int timeoutSecs)
{
    CommandResult result;
    result.exitCode = -1;
    result.timedOut = false;

    int stdoutPipe[2], stdinPipe[2];
    if (pipe(stdoutPipe) != 0 || pipe(stdinPipe) != 0) {
        result.stderr = "pipe failed";
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        result.stderr = "fork failed";
        close(stdoutPipe[0]); close(stdoutPipe[1]);
        close(stdinPipe[0]); close(stdinPipe[1]);
        return result;
    }

    if (pid == 0) {
        // Child
        close(stdoutPipe[0]);
        close(stdinPipe[1]);
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stdinPipe[0], STDIN_FILENO);
        close(stdoutPipe[1]);
        close(stdinPipe[0]);
        setpgid(0, 0);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127);
    }

    // Parent
    close(stdoutPipe[1]);
    close(stdinPipe[0]);

    // Write stdin
    if (!stdinData.empty()) {
        size_t written = 0;
        while (written < stdinData.size()) {
            ssize_t n = write(stdinPipe[1],
                              stdinData.data() + written,
                              stdinData.size() - written);
            if (n > 0) written += n;
            else break;
        }
    }
    close(stdinPipe[1]);

    // Set alarm for timeout
    if (timeoutSecs > 0) {
        g_timeoutFired = 0;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = alarmHandler;
        sigaction(SIGALRM, &sa, nullptr);
        alarm(static_cast<unsigned int>(timeoutSecs));
    }

    // Read stdout
    char buf[4096];
    ssize_t n;
    while ((n = read(stdoutPipe[0], buf, sizeof(buf))) > 0) {
        result.stdout.append(buf, static_cast<size_t>(n));
    }
    close(stdoutPipe[0]);

    // Cancel alarm
    alarm(0);

    // Kill child on timeout BEFORE waitpid so waitpid returns immediately
    if (g_timeoutFired) {
        kill(-pid, SIGKILL);
    }

    // Wait for child
    int status;
    waitpid(pid, &status, 0);

    if (g_timeoutFired) {
        result.timedOut = true;
        result.exitCode = -1;
        result.stderr = "timeout";
        return result;
    }

    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = -WTERMSIG(status);
    }

    return result;
}

} // namespace a0
