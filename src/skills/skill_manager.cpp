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

int SkillManager::getPrompt(const std::string& qualifiedName, Prompt& prompt) const
{
    std::string ns, component, promptName;
    if (!parseQualifiedName(qualifiedName, ns, component, promptName)) {
        return -2;
    }
    return m_loader->getPrompt(ns, component, promptName, prompt);
}

/// Recursively collect prompts from a chain list and append their resolved texts to `parts`.
/// chain entries are within the same component by default; qualified names (containing ':')
/// are resolved cross-component.
static void xCollectChain(const SkillLoader* loader,
                           const std::string& ns,
                           const std::string& component,
                           const std::vector<std::string>& chain,
                           std::vector<std::string>& parts) {
    for (const auto& chainName : chain) {
        Prompt cp;
        bool found = false;
        if (chainName.find(':') == std::string::npos) {
            // Short name: resolve within the same component
            if (loader->getPrompt(ns, component, chainName, cp) == 0) {
                found = true;
            }
        } else {
            // Qualified name: parse and resolve
            std::string ns2, comp2, name2;
            if (parseQualifiedName(chainName, ns2, comp2, name2) &&
                loader->getPrompt(ns2, comp2, name2, cp) == 0) {
                found = true;
            }
        }
        if (found) {
            // Recurse into the chain entry's own chain first
            if (!cp.chain.empty()) {
                xCollectChain(loader, ns, component, cp.chain, parts);
            }
            parts.push_back(cp.prompt);
        }
    }
}

int SkillManager::getPromptResolved(const std::string& qualifiedName, Prompt& out) const
{
    std::string ns, component, promptName;
    if (!parseQualifiedName(qualifiedName, ns, component, promptName)) {
        return -2;
    }
    // Load the target prompt
    Prompt target;
    int rc = m_loader->getPrompt(ns, component, promptName, target);
    if (rc != 0) return rc;

    // Copy metadata
    out.name = target.name;
    out.description = target.description;
    out.dependencies = target.dependencies;
    out.validators = target.validators;
    out.ns = ns;
    out.component = component;

    // Flatten chain recursively: chain entries' chains are resolved first,
    // then the chain entry's text, then the target's text
    std::vector<std::string> parts;
    if (!target.chain.empty()) {
        xCollectChain(m_loader, ns, component, target.chain, parts);
    }
    parts.push_back(target.prompt);

    // Concatenate with double-newline separators
    std::string combined;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) combined += "\n\n";
        combined += parts[i];
    }
    out.prompt = combined;
    return 0;
}

int SkillManager::resolveName(const std::string& componentNs,
                               const std::string& componentName,
                               const std::string& shortName,
                               std::string& qualifiedOut) const
{
    // If already qualified (contains ':'), use directly
    if (shortName.find(':') != std::string::npos) {
        qualifiedOut = shortName;
        return 0;
    }

    // Try within the same component first
    std::string within = buildQualifiedName(componentNs, componentName, shortName);
    SkillTool tool;
    Prompt prompt;
    if (m_loader->getTool(componentNs, componentName, shortName, tool) == 0 ||
        m_loader->getPrompt(componentNs, componentName, shortName, prompt) == 0) {
        qualifiedOut = within;
        return 0;
    }

    // Fall through: leave as-is; caller will handle unresolved
    qualifiedOut = shortName;
    return 1; // not found but no error — may resolve at call site
}

std::vector<std::string> SkillManager::listSkills(std::optional<SkillNamespace> ns) const
{
    if (ns.has_value()) {
        return m_loader->listComponents(ns.value());
    }
    std::vector<std::string> result;
    auto sys = m_loader->listComponents(SkillNamespace::SYSTEM);
    result.insert(result.end(), sys.begin(), sys.end());
    auto loc = m_loader->listComponents(SkillNamespace::LOCAL);
    result.insert(result.end(), loc.begin(), loc.end());
    auto git = m_loader->listComponents(SkillNamespace::GITHUB);
    result.insert(result.end(), git.begin(), git.end());
    return result;
}

int SkillManager::addTool(const std::string& component, const SkillTool& tool)
{
    return m_loader->addTool(component, tool);
}

int SkillManager::addPrompt(const std::string& component, const Prompt& prompt)
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
// Dispatch table
// ---------------------------------------------------------------------------

/// Split a string on a delimiter character.
static std::vector<std::string> xSplit(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delim)) {
        parts.push_back(part);
    }
    return parts;
}

/// Convert SkillNamespace enum to string prefix.
static std::string xNsToStr(SkillNamespace ns) {
    switch (ns) {
        case SkillNamespace::SYSTEM: return "system";
        case SkillNamespace::LOCAL:  return "local";
        case SkillNamespace::GITHUB: return "github";
    }
    return "";
}

std::unordered_map<std::string, std::string> SkillManager::buildDispatchTable() const {
    std::unordered_map<std::string, std::string> table;

    struct Entry { std::string qn; };
    std::vector<Entry> entries;

    // Collect every tool and prompt from every loaded namespace
    for (auto ns : {SkillNamespace::SYSTEM, SkillNamespace::LOCAL, SkillNamespace::GITHUB}) {
        auto components = m_loader->listComponents(ns);
        for (const auto& comp : components) {
            SkillManifest manifest;
            if (m_loader->getManifest(ns, comp, manifest) != 0) continue;
            std::string nsStr = xNsToStr(ns);

            for (const auto& tool : manifest.tools)
                entries.push_back({buildQualifiedName(nsStr, comp, tool.name)});
            for (const auto& prompt : manifest.prompts)
                entries.push_back({buildQualifiedName(nsStr, comp, prompt.name)});
        }
    }

    // Assign short names with collision resolution
    for (const auto& entry : entries) {
        std::string shortName = entry.qn.substr(entry.qn.rfind(':') + 1);
        std::string candidate = shortName;
        int attempt = 0;

        while (table.find(candidate) != table.end()) {
            auto parts = xSplit(entry.qn, ':');
            int n = (int)parts.size();
            // Build from rightmost segments: +2 because attempt 0 already tried the last segment
            int segments = std::min(attempt + 2, n);

            std::string rebuilt;
            for (int i = n - segments; i < n; ++i) {
                if (!rebuilt.empty()) rebuilt += '_';
                rebuilt += parts[i];
            }
            candidate = rebuilt;
            attempt++;
        }
        table[candidate] = entry.qn;
    }

    return table;
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
