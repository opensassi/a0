#include "tool_runner.h"
#include "trace.h"
#include <array>
#include <cstdio>
#include <memory>
#include <sstream>

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
    std::array<char, 128> buffer;
    std::string result;

    std::string fullCmd;
    if (!stdinData.empty()) {
        fullCmd = "printf '%s' " + shellEscape(stdinData) + " | " + cmd;
    } else {
        fullCmd = cmd;
    }

    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        return "ERROR: command not found: " + cmd;
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    int rc = pclose(pipe);
    if (rc != 0) {
        if (result.empty()) {
            return "ERROR: command failed with exit code " + std::to_string(rc);
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
    std::string input;
    if (tool.inputMode == "stdin") {
        if (params.is_object() && params.contains("input") && params["input"].is_string()) {
            input = params["input"].get<std::string>();
        } else if (params.is_string()) {
            input = params.get<std::string>();
        } else {
            input = params.dump();
        }
    }

    std::string output = exec(tool.command, input);
    return output;
}
