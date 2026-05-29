#include "tool_runner.h"
#include "command_runner.h"
#include "trace.h"

using a0::CommandRunner;
using a0::CommandResult;

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

json SubprocessToolRunner::run(const Tool& tool, const json& params)
{
    TRACE_LOG("run(" << tool.name << ")");

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
        auto result = CommandRunner::run(cmd, "", 30);
        return formatResult(result);
    }

    std::string input;
    if (params.is_object() && params.contains("input") && params["input"].is_string()) {
        input = params["input"].get<std::string>();
    } else if (params.is_string()) {
        input = params.get<std::string>();
    } else {
        input = params.dump();
    }

    auto result = CommandRunner::run(tool.command, input, 30);
    return formatResult(result);
}
