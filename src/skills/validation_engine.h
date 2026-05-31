#pragma once

#include "skills.h"

namespace a0::persistence { class PersistenceStore; }

namespace a0::skills {

/// Replays historical tool invocations against a candidate version.
/// Uses CommandRunner for all subprocess execution.
/// Invocation records are read from the persistence store (SQLite).
class ValidationEngine {
public:
    explicit ValidationEngine(a0::persistence::PersistenceStore* store);

    int validate(SkillNamespace ns,
                 const std::string& component,
                 const SkillManifest& manifest,
                 const std::string& commit,
                 std::string& report);

private:
    a0::persistence::PersistenceStore* m_store;

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
};

} // namespace a0::skills
