#include "tool_runner.h"
#include "command_runner.h"
#include "trace.h"
#include <unistd.h>

using a0::CommandRunner;
using a0::CommandResult;
using a0::StreamHandle;

static std::string formatResult(const CommandResult& res)
{
    if (res.timedOut) {
        return "ERROR: timeout";
    }
    if (res.exitCode != 0 && res.stdout.empty()) {
        return "ERROR: command failed with exit code " + std::to_string(res.exitCode);
    }
    const size_t maxSize = 1024 * 1024;
    std::string output = res.stdout;
    if (output.size() > maxSize) {
        output.resize(maxSize);
        output += "... (truncated)";
    }
    return output;
}

// Build the shell command and extract stdin payload from tool params
static void buildToolCommand(const Tool& tool, const json& params,
                             std::string& outCmd, std::string& outStdin)
{
    if (tool.inputMode == "args") {
        outCmd = tool.command;
        outStdin = "";
        if (params.is_object()) {
            for (auto& [key, value] : params.items()) {
                if (key == "_") {
                    if (value.is_string()) {
                        outCmd += " " + CommandRunner::shellEscape(value.get<std::string>());
                    } else {
                        outCmd += " " + CommandRunner::shellEscape(value.dump());
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
                    outCmd += " --" + key + "=" + CommandRunner::shellEscape(val);
                }
            }
        } else if (params.is_string()) {
            outCmd += " " + CommandRunner::shellEscape(params.get<std::string>());
        }
        return;
    }

    // stdin mode
    outCmd = tool.command;
    if (params.is_object() && params.contains("input") && params["input"].is_string()) {
        outStdin = params["input"].get<std::string>();
    } else if (params.is_string()) {
        outStdin = params.get<std::string>();
    } else {
        outStdin = params.dump();
    }
}

// Base class default streaming implementation
// Subclasses (like DockerToolRunnerImpl) may override with container-specific logic
a0::StreamHandle ToolRunner::runStreaming(const Tool& tool,
                                           const json& params,
                                           a0::StreamCallback onChunk)
{
    std::string cmd, input;
    buildToolCommand(tool, params, cmd, input);

    // Wrap shell command to also accept stdin via redirect
    auto handle = CommandRunner::runStreaming(cmd, onChunk, tool.timeoutSecs);
    // Send the initial input then close stdin so the child sees EOF
    if (!input.empty()) {
        handle.sendInput(input);
    }
    // Close stdin pipe so child processes can terminate (they don't wait for more input)
    if (handle.m_state) {
        std::lock_guard<std::mutex> lock(handle.m_state->mutex);
        if (handle.m_state->stdinFd >= 0) {
            close(handle.m_state->stdinFd);
            handle.m_state->stdinFd = -1;
        }
    }
    return handle;
}

json SubprocessToolRunner::run(const Tool& tool, const json& params)
{
    TRACE_LOG("run(" << tool.name << ")");

    std::string cmd, input;
    buildToolCommand(tool, params, cmd, input);
    auto result = CommandRunner::run(cmd, input, tool.timeoutSecs);
    return formatResult(result);
}

StreamHandle SubprocessToolRunner::runStreaming(const Tool& tool,
                                                 const json& params,
                                                 a0::StreamCallback onChunk)
{
    // Reuse base class default implementation
    return ToolRunner::runStreaming(tool, params, std::move(onChunk));
}
