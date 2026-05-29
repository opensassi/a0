#pragma once

#include "skills.h"
#include <string>
#include <unordered_map>

namespace a0::skills {

class VersionManager {
public:
    VersionManager(const std::string& storeRoot,
                   const std::string& skillsRoot);

    int archive(SkillNamespace ns,
                const std::string& component,
                const std::string& commit,
                const std::string& version);

    int restore(SkillNamespace ns,
                const std::string& component,
                const std::string& commit);

    int release(SkillNamespace ns,
                const std::string& component,
                const std::string& commit);

    int gc(bool dryRun = false);

private:
    std::string m_storeRoot;
    std::string m_skillsRoot;
    std::string m_lockPath;
    std::unordered_map<std::string, StoredVersion> m_versions;

    int xLoadLock();
    int xSaveLock();
    std::string xStorePath(SkillNamespace ns,
                           const std::string& commit,
                           const std::string& component) const;
    std::string xVersionKey(SkillNamespace ns,
                            const std::string& component,
                            const std::string& commit) const;
    std::string xActivePath(SkillNamespace ns,
                            const std::string& component) const;
    int xCopyDir(const std::string& src, const std::string& dst);
};

} // namespace a0::skills
