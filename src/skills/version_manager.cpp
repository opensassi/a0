#include "version_manager.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace a0::skills {

VersionManager::VersionManager(const std::string& storeRoot,
                               const std::string& skillsRoot)
    : m_storeRoot(storeRoot)
    , m_skillsRoot(skillsRoot)
    , m_lockPath(storeRoot + "/../lock.json")
{
    xLoadLock();
}

int VersionManager::archive(SkillNamespace ns,
                             const std::string& component,
                             const std::string& commit,
                             const std::string& version)
{
    std::string key = xVersionKey(ns, component, commit);
    auto it = m_versions.find(key);
    if (it != m_versions.end()) {
        it->second.refcount++;
        return xSaveLock();
    }

    std::string dst = xStorePath(ns, commit, component);
    std::string src = xActivePath(ns, component);
    mkdir(dst.c_str(), 0755);
    if (xCopyDir(src, dst) != 0) {
        return -1;
    }

    StoredVersion sv;
    sv.commitHash = commit;
    sv.version = version;
    sv.refcount = 1;
    sv.installedAt = time(nullptr);
    m_versions[key] = sv;
    return xSaveLock();
}

int VersionManager::restore(SkillNamespace ns,
                             const std::string& component,
                             const std::string& commit)
{
    std::string key = xVersionKey(ns, component, commit);
    auto it = m_versions.find(key);
    if (it == m_versions.end()) {
        return -1;
    }
    std::string src = xStorePath(ns, commit, component);
    std::string dst = xActivePath(ns, component);
    std::string rmCmd = "rm -rf " + dst;
    system(rmCmd.c_str());
    return xCopyDir(src, dst);
}

int VersionManager::release(SkillNamespace ns,
                             const std::string& component,
                             const std::string& commit)
{
    if (commit.empty()) {
        // Find the currently active version and release it
        for (auto& [key, sv] : m_versions) {
            if (key.find(xVersionKey(ns, component, "")) == 0) {
                sv.refcount--;
                if (sv.refcount < 0) sv.refcount = 0;
                return xSaveLock();
            }
        }
        return -1;
    }
    std::string key = xVersionKey(ns, component, commit);
    auto it = m_versions.find(key);
    if (it == m_versions.end()) {
        return -1;
    }
    it->second.refcount--;
    if (it->second.refcount < 0) it->second.refcount = 0;
    return xSaveLock();
}

int VersionManager::gc(bool dryRun)
{
    int removed = 0;
    auto it = m_versions.begin();
    while (it != m_versions.end()) {
        if (it->second.refcount <= 0) {
            if (!dryRun) {
                std::string dir = m_storeRoot + "/" + it->first;
                std::string cmd = "rm -rf " + dir;
                system(cmd.c_str());
                it = m_versions.erase(it);
            }
            removed++;
        } else {
            ++it;
        }
    }
    if (!dryRun) {
        xSaveLock();
    }
    return removed;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int VersionManager::xLoadLock()
{
    m_versions.clear();
    std::ifstream ifs(m_lockPath);
    if (!ifs) {
        return -1;
    }
    nlohmann::json j;
    try {
        ifs >> j;
    } catch (...) {
        return -1;
    }
    if (j.contains("versions")) {
        for (const auto& [key, v] : j["versions"].items()) {
            StoredVersion sv;
            sv.commitHash = v.value("commit", "");
            sv.version = v.value("version", "");
            sv.refcount = v.value("refcount", 0);
            sv.installedAt = v.value("installedAt", 0);
            m_versions[key] = sv;
        }
    }
    return 0;
}

int VersionManager::xSaveLock()
{
    nlohmann::json j;
    j["version"] = 1;
    for (const auto& [key, sv] : m_versions) {
        nlohmann::json v;
        v["commit"] = sv.commitHash;
        v["version"] = sv.version;
        v["refcount"] = sv.refcount;
        v["installedAt"] = sv.installedAt;
        j["versions"][key] = v;
    }

    std::string dir = m_lockPath.substr(0, m_lockPath.rfind('/'));
    mkdir(dir.c_str(), 0755);
    std::ofstream ofs(m_lockPath);
    if (!ofs) {
        return -1;
    }
    ofs << j.dump(2) << std::endl;
    return 0;
}

std::string VersionManager::xStorePath(SkillNamespace ns,
                                        const std::string& commit,
                                        const std::string& component) const
{
    std::string nsDir;
    switch (ns) {
        case SkillNamespace::SYSTEM: nsDir = "system"; break;
        case SkillNamespace::LOCAL:  nsDir = "local"; break;
        case SkillNamespace::GITHUB: nsDir = "github"; break;
    }
    return m_storeRoot + "/" + nsDir + "/" + commit + "/" + component;
}

std::string VersionManager::xVersionKey(SkillNamespace ns,
                                         const std::string& component,
                                         const std::string& commit) const
{
    std::string nsDir;
    switch (ns) {
        case SkillNamespace::SYSTEM: nsDir = "system"; break;
        case SkillNamespace::LOCAL:  nsDir = "local"; break;
        case SkillNamespace::GITHUB: nsDir = "github"; break;
    }
    if (commit.empty()) {
        return nsDir + ":" + component + ":";
    }
    return nsDir + ":" + component + ":" + commit;
}

std::string VersionManager::xActivePath(SkillNamespace ns,
                                         const std::string& component) const
{
    std::string nsDir;
    switch (ns) {
        case SkillNamespace::SYSTEM: nsDir = "system"; break;
        case SkillNamespace::LOCAL:  nsDir = "local"; break;
        case SkillNamespace::GITHUB: nsDir = "github_"; break;
    }
    return m_skillsRoot + "/" + nsDir + "/" + component;
}

int VersionManager::xCopyDir(const std::string& src, const std::string& dst)
{
    std::string cmd = "cp -r " + src + "/* " + dst;
    return system(cmd.c_str());
}

} // namespace a0::skills
