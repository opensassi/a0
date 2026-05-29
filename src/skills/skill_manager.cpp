#include "skills.h"
#include "skill_loader.h"
#include "version_manager.h"
#include "validation_engine.h"
#include <algorithm>
#include <sstream>
#include <cassert>

namespace a0::skills {

// ---------------------------------------------------------------------------
// Qualified name helpers
// ---------------------------------------------------------------------------

bool parseQualifiedName(const std::string& qualified,
                        std::string& ns,
                        std::string& component,
                        std::string& name)
{
    auto first = qualified.find(':');
    if (first == std::string::npos) {
        return false;
    }
    ns = qualified.substr(0, first);
    auto second = qualified.find(':', first + 1);
    if (second == std::string::npos) {
        component = qualified.substr(first + 1);
        name = component;
    } else {
        component = qualified.substr(first + 1, second - first - 1);
        name = qualified.substr(second + 1);
    }
    return true;
}

std::string buildQualifiedName(const std::string& ns,
                                const std::string& component,
                                const std::string& name)
{
    if (name.empty() || name == component) {
        return ns + ":" + component;
    }
    return ns + ":" + component + ":" + name;
}

// ---------------------------------------------------------------------------
// SkillManager
// ---------------------------------------------------------------------------

SkillManager::SkillManager(const std::string& skillsRoot,
                           const std::string& storeRoot,
                           const std::string& logDir)
    : m_skillsRoot(skillsRoot)
    , m_storeRoot(storeRoot)
    , m_logDir(logDir)
    , m_loader(new SkillLoader(skillsRoot))
    , m_versionMgr(new VersionManager(storeRoot, skillsRoot))
    , m_validator(new ValidationEngine(logDir))
{
}

SkillManager::~SkillManager()
{
    delete m_loader;
    delete m_versionMgr;
    delete m_validator;
}

int SkillManager::loadAll()
{
    return m_loader->loadAll();
}

int SkillManager::getTool(const std::string& qualifiedName, SkillTool& tool) const
{
    std::string ns, component, toolName;
    if (!parseQualifiedName(qualifiedName, ns, component, toolName)) {
        return -2;
    }
    return m_loader->getTool(ns, component, toolName, tool);
}

int SkillManager::getPrompt(const std::string& qualifiedName, SkillPrompt& prompt) const
{
    std::string ns, component, promptName;
    if (!parseQualifiedName(qualifiedName, ns, component, promptName)) {
        return -2;
    }
    return m_loader->getPrompt(ns, component, promptName, prompt);
}

std::vector<std::string> SkillManager::listSkills(std::optional<SkillNamespace> ns) const
{
    std::vector<std::string> result;
    auto all = m_loader->listComponents(SkillNamespace::SYSTEM);
    all.insert(all.end(),
               m_loader->listComponents(SkillNamespace::LOCAL).begin(),
               m_loader->listComponents(SkillNamespace::LOCAL).end());
    all.insert(all.end(),
               m_loader->listComponents(SkillNamespace::GITHUB).begin(),
               m_loader->listComponents(SkillNamespace::GITHUB).end());
    return all;
}

int SkillManager::addTool(const std::string& component, const SkillTool& tool)
{
    return m_loader->addTool(component, tool);
}

int SkillManager::addPrompt(const std::string& component, const SkillPrompt& prompt)
{
    return m_loader->addPrompt(component, prompt);
}

int SkillManager::updateTool(const std::string& component,
                              const std::string& name,
                              const SkillTool& tool)
{
    return m_loader->updateTool(component, name, tool);
}

int SkillManager::install(const std::string& sourceUrl, bool force)
{
    SkillManifest manifest;
    SkillNamespace ns = SkillNamespace::GITHUB;
    return xInstallFromGit(sourceUrl, "", force, ns, manifest);
}

int SkillManager::install(const std::string& sourceUrl,
                           const std::string& commit,
                           bool force)
{
    SkillManifest manifest;
    SkillNamespace ns = SkillNamespace::GITHUB;
    return xInstallFromGit(sourceUrl, commit, force, ns, manifest);
}

int SkillManager::remove(const std::string& qualifiedName)
{
    std::string ns, component, name;
    if (!parseQualifiedName(qualifiedName, ns, component, name)) {
        return -1;
    }
    SkillNamespace nsEnum;
    if (xEnsureNs(ns, nsEnum) != 0) {
        return -1;
    }
    if (nsEnum == SkillNamespace::SYSTEM) {
        return -1;
    }
    m_versionMgr->release(nsEnum, component, "");
    return m_loader->removeComponent(component);
}

int SkillManager::gc(bool dryRun)
{
    return m_versionMgr->gc(dryRun);
}

int SkillManager::validate(const std::string& qualifiedName,
                            const std::string& commit,
                            std::string& report)
{
    std::string ns, component, name;
    if (!parseQualifiedName(qualifiedName, ns, component, name)) {
        report = "invalid qualified name: " + qualifiedName;
        return -1;
    }
    SkillNamespace nsEnum;
    if (xEnsureNs(ns, nsEnum) != 0) {
        report = "unknown namespace: " + ns;
        return -1;
    }
    SkillManifest manifest;
    m_loader->getManifest(nsEnum, component, manifest);
    return m_validator->validate(nsEnum, component, manifest, commit, report);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

int SkillManager::xEnsureNs(const std::string& ns, SkillNamespace& outNs) const
{
    if (ns == "system") { outNs = SkillNamespace::SYSTEM; return 0; }
    if (ns == "local") { outNs = SkillNamespace::LOCAL; return 0; }
    if (ns.rfind("github_", 0) == 0) { outNs = SkillNamespace::GITHUB; return 0; }
    return -1;
}

int SkillManager::xInstallFromGit(const std::string& url,
                                   const std::string& commit,
                                   bool force,
                                   SkillNamespace ns,
                                   SkillManifest& manifest)
{
    (void)url;
    (void)commit;
    (void)force;
    (void)ns;
    (void)manifest;
    // TODO: Phase 5 — git clone, parse manifest, validate, archive
    return 0;
}

} // namespace a0::skills
