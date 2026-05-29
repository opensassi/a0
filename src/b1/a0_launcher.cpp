#include "a0_launcher.h"
#include "command_runner.h"
#include "nlohmann/json.hpp"
#include <sstream>

namespace a0::b1 {

A0Launcher::A0Launcher(const std::string& a0Binary)
    : m_a0Binary(a0Binary)
{
}

int A0Launcher::runSkill(const std::string& skill,
                          const std::string& params,
                          std::string& result,
                          int timeoutSeconds)
{
    std::string binary = m_a0Binary.empty() ? "a0" : m_a0Binary;
    std::string escapedParams = CommandRunner::shellEscape(params);

    std::ostringstream cmd;
    cmd << binary
        << " --no-b1"
        << " --run " << CommandRunner::shellEscape(skill)
        << " --params " << escapedParams;

    auto cmdResult = CommandRunner::run(cmd.str(), "", timeoutSeconds);

    if (cmdResult.timedOut) {
        return -2;
    }

    result = cmdResult.stdout;

    if (cmdResult.exitCode != 0) {
        return -1;
    }

    try {
        nlohmann::json parsed = nlohmann::json::parse(result);
        (void)parsed;
    } catch (...) {
        return -1;
    }

    return 0;
}

} // namespace a0::b1
