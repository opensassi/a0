#include "skills.h"
#include "skill_loader.h"
#include "version_manager.h"
#include "validation_engine.h"
#include "executor/tool_runner.h"
#include "persistence/persistence_store.h"
#include <algorithm>
#include <sstream>
#include <cassert>

namespace a0::skills {

// Convert SkillTool to legacy Tool for ToolRunner dispatch.
static ::Tool skillToolToTool(const SkillTool& st) {
    ::Tool t;
    t.name = st.name;
    t.description = st.description;
    t.command = st.command;
    t.inputMode = st.inputMode;
    t.dockerImage = st.dockerImage;
    t.trustLevel = st.trustLevel;
    t.aptDependencies = st.aptDependencies;
    t.timeoutSecs = st.timeoutSecs;
    return t;
}

// ---------------------------------------------------------------------------
// Qualified name helpers
// ---------------------------------------------------------------------------

bool parseQualifiedName(const std::string& qualified,
                        std::string& ns,
                        std::string& component,
                        std::string& name)
{
    // github_<user> namespace contains '_' internally — find the second '_'
    size_t first;
    if (qualified.rfind("github_", 0) == 0) {
        first = qualified.find('_', 7); // skip past "github_"
        if (first == std::string::npos) return false;
    } else {
        first = qualified.find('_');
        if (first == std::string::npos) return false;
    }
    ns = qualified.substr(0, first);
    // Everything after the namespace prefix
    auto rest = qualified.substr(first + 1);
    // Component names never use '_' (they use '-'), so the first '_' in rest
    // is always the component-name separator.
    auto sep = rest.find('_');
    if (sep == std::string::npos) {
        // 2 segments: ns_name → component = name
        component = rest;
        name = rest;
    } else {
        // 3+ segments: ns_component_name
        component = rest.substr(0, sep);
        name = rest.substr(sep + 1);
    }
    return true;
}

std::string buildQualifiedName(const std::string& ns,
                                const std::string& component,
                                const std::string& name)
{
    if (name.empty() || name == component) {
        return ns + "_" + component;
    }
    return ns + "_" + component + "_" + name;
}

// ---------------------------------------------------------------------------
// SkillManager
// ---------------------------------------------------------------------------

SkillManager::SkillManager(const std::string& skillsRoot,
                           const std::string& storeRoot,
                           ::a0::persistence::PersistenceStore* persistence)
    : m_skillsRoot(skillsRoot)
    , m_storeRoot(storeRoot)
    , m_loader(new SkillLoader(skillsRoot))
    , m_versionMgr(new VersionManager(storeRoot, skillsRoot))
    , m_validator(new ValidationEngine(persistence))
    , m_persistence(persistence)
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
/// chain entries are within the same component by default; qualified names containing '-'
/// are resolved cross-component (tool names never contain '-').
static void xCollectChain(const SkillLoader* loader,
                           const std::string& ns,
                           const std::string& component,
                           const std::vector<std::string>& chain,
                           std::vector<std::string>& parts) {
    for (const auto& chainName : chain) {
        Prompt cp;
        bool found = false;
        if (chainName.rfind("system_", 0) != 0 &&
            chainName.rfind("local_", 0) != 0 &&
            chainName.rfind("github_", 0) != 0) {
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
    // If already qualified (starts with known ns prefix), use directly
    if (shortName.rfind("system_", 0) == 0 ||
        shortName.rfind("local_", 0) == 0 ||
        shortName.rfind("github_", 0) == 0) {
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

int SkillManager::getManifest(SkillNamespace ns, const std::string& component, SkillManifest& manifest) const
{
    return m_loader->getManifest(ns, component, manifest);
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
        std::string shortName = entry.qn.substr(entry.qn.rfind('_') + 1);
        std::string candidate = shortName;
        int attempt = 0;

        while (table.find(candidate) != table.end()) {
            auto parts = xSplit(entry.qn, '_');
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

void SkillManager::setRecordingSession(int64_t sessionDbId)
{
    m_sessionDbId = sessionDbId;
}

/// Convert namespace string to type int for ensureSkill.
static int xNsToType(const std::string& ns) {
    if (ns == "system") return 0;
    if (ns == "local") return 1;
    return 2;
}

// ---------------------------------------------------------------------------
// Handler registry
// ---------------------------------------------------------------------------

void SkillManager::registerHandler(const std::string& qualifiedName, ToolHandler handler)
{
    m_handlers[qualifiedName] = std::move(handler);
}

void SkillManager::setToolRunner(::ToolRunner* runner)
{
    m_toolRunner = runner;
}

void SkillManager::setDockerRunner(::DockerToolRunner* runner)
{
    m_dockerRunner = runner;
}

void SkillManager::setDockerSecurityFilter(::a0::DockerSecurityFilter* filter)
{
    m_dockerSecurityFilter = filter;
}

json SkillManager::executeTool(const std::string& qualifiedName, const json& params)
{
    return executeToolWithMeta(qualifiedName, params, nullptr, "", 0).output;
}

::a0::HandlerResult SkillManager::executeToolWithMeta(
    const std::string& qualifiedName, const json& params,
    int* seq, const std::string& toolCallId, int64_t subSessionId)
{
    std::string output;
    std::vector<std::string> recommendedTools;

    // 1. Exact match in registered C++ handlers
    {
        auto it = m_handlers.find(qualifiedName);
        if (it != m_handlers.end()) {
            auto hr = it->second(params, HandlerContext{"", &m_toolState});
            output = hr.output;
            recommendedTools = hr.recommendedTools;
            goto record;
        }
    }

    // 1b. 2-part name alias: "system_bash" → try "system_bash_bash"
    {
        std::string ns, comp, name;
        if (parseQualifiedName(qualifiedName, ns, comp, name) && !name.empty()) {
            std::string qn3 = ns + "_" + comp + "_" + name;
            if (qn3 != qualifiedName) {
                auto it = m_handlers.find(qn3);
                if (it != m_handlers.end()) {
                    auto hr = it->second(params, HandlerContext{"", &m_toolState});
                    output = hr.output;
                    recommendedTools = hr.recommendedTools;
                    goto record;
                }
            }
        }
    }

    // 2. Wildcard match (ns_component_*)
    {
        std::string ns, comp, name;
        if (parseQualifiedName(qualifiedName, ns, comp, name)) {
            std::string wildcard = ns + "_" + comp + "_*";
            auto wit = m_handlers.find(wildcard);
            if (wit != m_handlers.end()) {
                std::string sub = name;
                // Check for subCommand override in tool definition
                SkillTool tool;
                if (getTool(qualifiedName, tool) == 0 && !tool.subCommand.empty()) {
                    sub = tool.subCommand;
                }
                auto hr = wit->second(params, HandlerContext{sub, &m_toolState});
                output = hr.output;
                recommendedTools = hr.recommendedTools;
                goto record;
            }
        }
    }

    // 3. Look up the tool definition
    {
        SkillTool tool;
        if (getTool(qualifiedName, tool) != 0) {
            output = "ERROR: tool not found: " + qualifiedName;
            goto record;
        }

        // 4. System tool with no handler registered
        if (tool.systemTool) {
            output = "ERROR: no C++ handler registered for system tool: " + qualifiedName;
            goto record;
        }

        // 5. Non-system tool: run via ToolRunner
        if (m_toolRunner) {
            ::ToolRunner* runner = (!tool.dockerImage.empty() && m_dockerRunner)
                ? m_dockerRunner : m_toolRunner;
            auto r = runner->run(skillToolToTool(tool), params);
            if (r.is_string()) output = r;
            else output = r.dump();
            goto record;
        }

        output = "ERROR: no ToolRunner available for command tool: " + qualifiedName;
    }

record:
    if (m_sessionDbId > 0 && m_persistence && seq) {
        int currentSeq = (*seq)++;
        int64_t msgId = m_persistence->appendMessage(m_sessionDbId,
            subSessionId != 0 ? std::optional<int64_t>(subSessionId) : std::nullopt,
            currentSeq, "tool", output, "", toolCallId, qualifiedName, "");

        std::string ns, comp, tName;
        if (parseQualifiedName(qualifiedName, ns, comp, tName)) {
            int sType = xNsToType(ns);
            int skillId = m_persistence->ensureSkill(sType, comp);
            m_persistence->appendInvocation(msgId, skillId, tName, params.dump(), output);
        }
    }

    return {output, recommendedTools};
}

a0::StreamHandle SkillManager::executeToolStreaming(
    const std::string& qualifiedName, const json& params,
    a0::StreamCallback onChunk,
    int* seq, const std::string& toolCallId, int64_t subSessionId)
{
    // 1. Check for system tool handler (C++ handlers are synchronous — run once, then complete)
    {
        auto it = m_handlers.find(qualifiedName);
        if (it != m_handlers.end()) {
            auto hr = it->second(params, HandlerContext{"", &m_toolState});
            onChunk(hr.output, "stdout");
            a0::StreamHandle h;
            return h;
        }
    }

    // 2. Wildcard match
    {
        std::string ns, comp, name;
        if (parseQualifiedName(qualifiedName, ns, comp, name)) {
            std::string wildcard = ns + "_" + comp + "_*";
            auto wit = m_handlers.find(wildcard);
            if (wit != m_handlers.end()) {
                std::string sub = name;
                SkillTool tool;
                if (getTool(qualifiedName, tool) == 0 && !tool.subCommand.empty())
                    sub = tool.subCommand;
                auto hr = wit->second(params, HandlerContext{sub, &m_toolState});
                onChunk(hr.output, "stdout");
                a0::StreamHandle h;
                return h;
            }
        }
    }

    // 3. Look up tool definition
    SkillTool tool;
    if (getTool(qualifiedName, tool) != 0) {
        onChunk("ERROR: tool not found: " + qualifiedName, "stdout");
        return {};
    }

    if (tool.systemTool) {
        onChunk("ERROR: system tools do not support streaming: " + qualifiedName, "stdout");
        return {};
    }

    // 4. Command tool — delegate to ToolRunner::runStreaming
    ::Tool legacy = skillToolToTool(tool);
    ::ToolRunner* runner = (!tool.dockerImage.empty() && m_dockerRunner)
        ? m_dockerRunner : m_toolRunner;

    if (!runner) {
        onChunk("ERROR: no ToolRunner available", "stdout");
        return {};
    }

    auto handle = runner->runStreaming(legacy, params, std::move(onChunk));

    // Record to persistence if active
    if (m_sessionDbId > 0 && m_persistence && seq) {
        int currentSeq = (*seq)++;
        m_persistence->appendMessage(m_sessionDbId,
            subSessionId != 0 ? std::optional<int64_t>(subSessionId) : std::nullopt,
            currentSeq, "tool", "", "", toolCallId, qualifiedName, "");

        std::string ns, comp, tName;
        if (parseQualifiedName(qualifiedName, ns, comp, tName)) {
            int sType = (ns == "system") ? 0 : (ns == "local") ? 1 : 2;
            int skillId = m_persistence->ensureSkill(sType, comp);
            m_persistence->appendInvocation(0, skillId, tName, params.dump(), "");
        }
    }

    return handle;
}

std::vector<std::string> SkillManager::missingHandlers() const
{
    std::vector<std::string> missing;
    for (auto ns : {SkillNamespace::SYSTEM, SkillNamespace::LOCAL, SkillNamespace::GITHUB}) {
        auto components = m_loader->listComponents(ns);
        for (const auto& comp : components) {
            SkillManifest manifest;
            if (m_loader->getManifest(ns, comp, manifest) != 0) continue;
            std::string nsStr = (ns == SkillNamespace::SYSTEM) ? "system"
                              : (ns == SkillNamespace::LOCAL) ? "local" : "github";
            for (const auto& tool : manifest.tools) {
                if (!tool.systemTool) continue;
                std::string qn3 = nsStr + "_" + comp + "_" + tool.name;
                if (m_handlers.find(qn3) != m_handlers.end()) continue;
                if (m_handlers.find(nsStr + "_" + comp + "_*") != m_handlers.end()) continue;
                if (tool.name == comp && m_handlers.find(nsStr + "_" + comp) != m_handlers.end()) continue;
                missing.push_back(qn3);
            }
        }
    }
    return missing;
}

std::vector<::ToolSchema> SkillManager::schemas(bool defaultOnly) const
{
    std::vector<::ToolSchema> result;
    for (auto ns : {SkillNamespace::SYSTEM, SkillNamespace::LOCAL, SkillNamespace::GITHUB}) {
        auto components = m_loader->listComponents(ns);
        for (const auto& comp : components) {
            SkillManifest manifest;
            if (m_loader->getManifest(ns, comp, manifest) != 0) continue;
            for (const auto& tool : manifest.tools) {
                if (defaultOnly && !tool.default_) continue;
                if (tool.parameters.is_null() || tool.parameters.empty()) continue;
                ::ToolSchema ts;
                ts.name = tool.name;
                ts.description = tool.description;
                ts.inputSchema = tool.parameters;
                result.push_back(std::move(ts));
            }
        }
    }
    return result;
}

} // namespace a0::skills
