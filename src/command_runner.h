#pragma once

#include <string>
#include <vector>

namespace a0 {

struct CommandResult {
    int exitCode;
    std::string stdout;
    std::string stderr;
    bool timedOut;
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

    /// Shell-escape a string for safe use in sh -c.
    static std::string shellEscape(const std::string& s);

private:
    static CommandResult xRunSingle(const std::string& cmd,
                                     const std::string& stdinData,
                                     int timeoutSecs);
};

} // namespace a0
