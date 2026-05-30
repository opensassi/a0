#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

namespace a0 {

struct CommandResult {
    int exitCode;
    std::string stdout;
    std::string stderr;
    bool timedOut;
};

/// Callback for streaming output: data chunk + direction ("stdout"|"stderr").
using StreamCallback = std::function<void(const std::string& data,
                                           const std::string& direction)>;

/// Handle for a running streaming process.
struct StreamHandle {
    int64_t streamId = 0;
    int pid = 0;

    /// Non-blocking check if process has exited.
    bool isDone() const;

    /// Block until process exits. Returns exit code.
    int wait();

    /// Send SIGTERM (2s grace), then SIGKILL.
    void cancel();

    /// Write data to the process's stdin.
    void sendInput(const std::string& data);

    // Internal state (shared with the reader thread)
    struct State {
        std::mutex mutex;
        int stdinFd = -1;
        pid_t childPid = 0;
        std::atomic<bool> done{false};
        std::atomic<int> exitCode{-1};
        std::thread thread;
    };
    std::shared_ptr<State> m_state;
};

/// Stateless utility class wrapping all subprocess creation.
/// No other class in the codebase calls fork/exec/pipe directly.
class CommandRunner {
public:
    /// Run a single command synchronously.
    /// \param cmd         Shell command string (passed to sh -c).
    /// \param stdinData   Optional data piped to stdin.
    /// \param timeoutSecs Max seconds before SIGKILL (0 = no timeout).
    static CommandResult run(const std::string& cmd,
                              const std::string& stdinData = "",
                              int timeoutSecs = 30);

    /// Run multiple commands in parallel.
    /// Forks N children, waits for all, collects results.
    /// \param cmds         Vector of command strings.
    /// \param timeoutSecs  Per-command timeout.
    /// \param maxParallel  Max concurrent children (0 = unlimited).
    static std::vector<CommandResult> runAll(
        const std::vector<std::string>& cmds,
        int timeoutSecs = 30,
        int maxParallel = 4);

    /// Run a command with streaming output.
    /// Returns immediately with a StreamHandle.
    /// The callback is called from a background thread for each chunk.
    /// \param cmd         Shell command string.
    /// \param onChunk     Called with (data, "stdout"|"stderr") for each read.
    /// \param timeoutSecs Max seconds before SIGKILL.
    static StreamHandle runStreaming(const std::string& cmd,
                                      StreamCallback onChunk,
                                      int timeoutSecs = 30);

    /// Shell-escape a string for safe use in sh -c.
    static std::string shellEscape(const std::string& s);

private:
    static CommandResult xRunSingle(const std::string& cmd,
                                     const std::string& stdinData,
                                     int timeoutSecs);
};

} // namespace a0
