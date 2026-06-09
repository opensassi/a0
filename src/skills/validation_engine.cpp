#include "validation_engine.h"
#include "executor/command_runner.h"
#include "persistence/persistence_store.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace a0::skills {

using a0::CommandRunner;

static int xNsToType(SkillNamespace ns) {
    switch (ns) {
        case SkillNamespace::SYSTEM: return 0;
        case SkillNamespace::LOCAL:  return 1;
        case SkillNamespace::GITHUB: return 2;
    }
    return 0;
}

ValidationEngine::ValidationEngine(::a0::persistence::PersistenceStore* store)
    : m_store(store)
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
    if (!m_store) return records;

    int type = xNsToType(
        ns == "system" ? SkillNamespace::SYSTEM :
        ns == "local"  ? SkillNamespace::LOCAL :
                         SkillNamespace::GITHUB);

    auto rows = m_store->loadInvocations(type, component);
    for (const auto& row : rows) {
        InvocationRecord rec;
        rec.toolName = row.toolName;
        try {
            rec.params = nlohmann::json::parse(row.paramsJson);
        } catch (...) {
            rec.params = row.paramsJson;
        }
        try {
            rec.output = nlohmann::json::parse(row.outputJson);
        } catch (...) {
            rec.output = row.outputJson;
        }
        rec.timestamp = row.timestamp;
        records.push_back(rec);
    }
    return records;
}

} // namespace a0::skills
