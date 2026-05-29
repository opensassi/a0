#pragma once

#include "skills.h"

namespace a0::skills {

/// Replays historical tool invocations against a candidate version.
/// Uses CommandRunner for all subprocess execution.
class ValidationEngine {
public:
    explicit ValidationEngine(const std::string& logDir);

    int validate(SkillNamespace ns,
                 const std::string& component,
                 const SkillManifest& manifest,
                 const std::string& commit,
                 std::string& report);

private:
    std::string m_logDir;

    int xReplay(const InvocationRecord& record,
                const SkillManifest& manifest,
                const std::string& toolName,
                nlohmann::json& actualOutput);
    int xCompare(const nlohmann::json& expected,
                 const nlohmann::json& actual,
                 const ToolSchema& schema);
    int xApplyBridge(const CompatBridge& bridge,
                     const nlohmann::json& input,
                     nlohmann::json& output);
    std::vector<InvocationRecord> xLoadLogs(const std::string& ns,
                                             const std::string& component) const;
    void xWriteReport(const std::string& path, const std::string& details);
};

} // namespace a0::skills
