#include "validation_engine.h"
#include "../command_runner.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

namespace a0::skills {

using a0::CommandRunner;

ValidationEngine::ValidationEngine(const std::string& logDir)
    : m_logDir(logDir)
{
}

int ValidationEngine::validate(SkillNamespace ns,
                                const std::string& component,
                                const SkillManifest& manifest,
                                const std::string& commit,
                                std::string& report)
{
    std::string nsStr;
    switch (ns) {
        case SkillNamespace::SYSTEM: nsStr = "system"; break;
        case SkillNamespace::LOCAL:  nsStr = "local"; break;
        case SkillNamespace::GITHUB: nsStr = "github"; break;
    }

    auto logs = xLoadLogs(nsStr, component);
    if (logs.empty()) {
        report = "no historical logs — validation skipped";
        return 0;
    }

    int failures = 0;
    int bridgesUsed = 0;
    std::ostringstream ss;

    for (const auto& record : logs) {
        nlohmann::json actualOutput;
        int ret = xReplay(record, manifest, record.toolName, actualOutput);
        if (ret != 0) {
            continue;
        }

        ToolSchema schema;
        bool hasSchema = false;
        for (const auto& t : manifest.tools) {
            if (t.name == record.toolName) {
                schema = t.schema;
                hasSchema = true;
                break;
            }
        }

        int cmp = xCompare(record.output, actualOutput, schema);
        if (cmp == 0) {
            continue;
        }

        bool bridgeApplied = false;
        for (const auto& bridge : manifest.compat) {
            if (bridge.toolName == record.toolName) {
                nlohmann::json bridgedOutput;
                if (xApplyBridge(bridge, record.params, bridgedOutput) == 0) {
                    if (xCompare(record.output, bridgedOutput, schema) == 0) {
                        bridgeApplied = true;
                        bridgesUsed++;
                        break;
                    }
                }
            }
        }

        if (!bridgeApplied) {
            failures++;
            ss << "FAIL: " << record.toolName
               << " at timestamp " << record.timestamp
               << " — output mismatch" << std::endl;
        }
    }

    std::string reportPath = m_logDir + "/../store/" + nsStr + "/" + commit + "/validation-report.txt";
    mkdir(reportPath.substr(0, reportPath.rfind('/')).c_str(), 0755);
    xWriteReport(reportPath, ss.str());

    report = ss.str();
    if (failures > 0) {
        return -1;
    }
    if (bridgesUsed > 0) {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int ValidationEngine::xReplay(const InvocationRecord& record,
                               const SkillManifest& manifest,
                               const std::string& toolName,
                               nlohmann::json& actualOutput)
{
    const SkillTool* matchedTool = nullptr;
    for (const auto& t : manifest.tools) {
        if (t.name == toolName) {
            matchedTool = &t;
            break;
        }
    }
    if (!matchedTool) {
        return -1;
    }

    // Build command and run via CommandRunner
    std::string cmd = matchedTool->command;
    std::string stdinData;
    if (record.params.is_string()) {
        stdinData = record.params.get<std::string>();
    } else {
        stdinData = record.params.dump();
    }

    auto result = CommandRunner::run(cmd, stdinData, 30);
    if (result.timedOut || result.exitCode != 0) {
        actualOutput = result.stderr;
        return 0;
    }

    try {
        actualOutput = nlohmann::json::parse(result.stdout);
    } catch (...) {
        actualOutput = result.stdout;
    }
    return 0;
}

int ValidationEngine::xCompare(const nlohmann::json& expected,
                                const nlohmann::json& actual,
                                const ToolSchema& schema)
{
    (void)schema;
    if (expected == actual) {
        return 0;
    }
    return -1;
}

int ValidationEngine::xApplyBridge(const CompatBridge& bridge,
                                    const nlohmann::json& input,
                                    nlohmann::json& output)
{
    std::string stdinData = input.dump();
    auto result = CommandRunner::run(bridge.bridgeCommand, stdinData, 30);
    if (result.exitCode != 0) {
        return -1;
    }
    try {
        output = nlohmann::json::parse(result.stdout);
    } catch (...) {
        output = result.stdout;
    }
    return 0;
}

std::vector<InvocationRecord> ValidationEngine::xLoadLogs(
    const std::string& ns,
    const std::string& component) const
{
    std::vector<InvocationRecord> records;
    std::string dir = m_logDir + "/" + ns + ":" + component;
    DIR* d = opendir(dir.c_str());
    if (!d) {
        return records;
    }

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name(entry->d_name);
        if (name.length() < 6 || name.rfind(".jsonl") != name.length() - 6) {
            continue;
        }
        std::string path = dir + "/" + name;
        std::ifstream ifs(path);
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            try {
                auto j = nlohmann::json::parse(line);
                InvocationRecord rec;
                rec.toolName = j.value("tool", "");
                rec.params = j.value("params", nlohmann::json::object());
                rec.output = j.value("output", nlohmann::json::object());
                rec.timestamp = j.value("ts", 0LL);
                records.push_back(rec);
            } catch (...) {
            }
        }
    }
    closedir(d);
    return records;
}

void ValidationEngine::xWriteReport(const std::string& path, const std::string& details)
{
    std::ofstream ofs(path);
    if (ofs) {
        ofs << details;
    }
}

} // namespace a0::skills
