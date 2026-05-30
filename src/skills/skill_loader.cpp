#include "skill_loader.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <dirent.h>
#include <sys/stat.h>

namespace a0::skills {

SkillLoader::SkillLoader(const std::string& root)
    : m_root(root)
{
}

int SkillLoader::loadAll()
{
    m_components.clear();
    m_nsMap.clear();

    if (DIR* dir = opendir(m_root.c_str())) {
        closedir(dir);
    } else {
        return -1;
    }

    xLoadNamespace(m_root + "/system", SkillNamespace::SYSTEM);
    xLoadNamespace(m_root + "/local", SkillNamespace::LOCAL);

    // github_<user> namespaces
    DIR* rootDir = opendir(m_root.c_str());
    if (rootDir) {
        struct dirent* entry;
        while ((entry = readdir(rootDir)) != nullptr) {
            std::string name(entry->d_name);
            if (name.rfind("github_", 0) == 0) {
                struct stat st;
                std::string path = m_root + "/" + name;
                if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    xLoadNamespace(path, SkillNamespace::GITHUB);
                }
            }
        }
        closedir(rootDir);
    }

    return 0;
}

int SkillLoader::getTool(const std::string& ns,
                          const std::string& component,
                          const std::string& toolName,
                          SkillTool& tool) const
{
    std::string key = xIndexKey(xNsForDir(ns), component);
    auto it = m_components.find(key);
    if (it == m_components.end()) {
        return -1;
    }
    for (const auto& t : it->second.tools) {
        if (t.name == toolName) {
            tool = t;
            return 0;
        }
    }
    return -2;
}

int SkillLoader::getPrompt(const std::string& ns,
                            const std::string& component,
                            const std::string& promptName,
                            Prompt& prompt) const
{
    std::string key = xIndexKey(xNsForDir(ns), component);
    auto it = m_components.find(key);
    if (it == m_components.end()) {
        return -1;
    }
    for (const auto& p : it->second.prompts) {
        if (p.name == promptName) {
            prompt = p;
            return 0;
        }
    }
    return -2;
}

std::vector<std::string> SkillLoader::listComponents(SkillNamespace ns) const
{
    std::vector<std::string> result;
    for (const auto& [key, manifest] : m_components) {
        if (manifest.ns == ns) {
            result.push_back(manifest.name);
        }
    }
    return result;
}

int SkillLoader::writeManifest(const std::string& component, const SkillManifest& manifest)
{
    if (xIsReadOnly(manifest.ns)) {
        return -1;
    }
    std::string dir = m_root + "/" + xDirForNamespace(manifest.ns) + "/" + component;
    std::string path = dir + "/skill.json";

    nlohmann::json j;
    j["name"] = manifest.name;
    j["version"] = manifest.version;
    j["description"] = manifest.description;
    for (const auto& t : manifest.tools) {
        nlohmann::json jt;
        jt["name"] = t.name;
        jt["description"] = t.description;
        jt["command"] = t.command;
        jt["inputMode"] = t.inputMode;
        if (t.systemTool) jt["systemTool"] = true;
        if (t.timeoutSecs != 30) jt["timeoutSecs"] = t.timeoutSecs;
        if (!t.dockerImage.empty()) jt["dockerImage"] = t.dockerImage;
        switch (t.trustLevel) {
            case TrustLevel::HIGH: jt["trustLevel"] = "HIGH"; break;
            case TrustLevel::LOW:  jt["trustLevel"] = "LOW"; break;
            default:               jt["trustLevel"] = "MEDIUM"; break;
        }
        if (!t.aptDependencies.empty()) jt["aptDependencies"] = t.aptDependencies;
        j["tools"].push_back(jt);
    }
    for (const auto& p : manifest.prompts) {
        nlohmann::json jp;
        jp["name"] = p.name;
        jp["description"] = p.description;
        jp["prompt"] = p.prompt;
        if (!p.dependencies.empty()) jp["dependencies"] = p.dependencies;
        if (!p.chain.empty()) jp["chain"] = p.chain;
        if (!p.validators.empty()) {
            for (const auto& v : p.validators) {
                nlohmann::json jv;
                jv["toolName"] = v.toolName;
                jp["validators"].push_back(jv);
            }
        }
        j["prompts"].push_back(jp);
    }

    mkdir(dir.c_str(), 0755);
    std::ofstream ofs(path);
    if (!ofs) {
        return -1;
    }
    ofs << j.dump(2) << std::endl;
    return 0;
}

int SkillLoader::readManifest(const std::string& path, SkillManifest& manifest) const
{
    std::ifstream ifs(path);
    if (!ifs) {
        return -1;
    }
    nlohmann::json j;
    try {
        ifs >> j;
    } catch (...) {
        return -1;
    }
    manifest.name = j.value("name", "");
    manifest.version = j.value("version", "0.0.0");
    manifest.description = j.value("description", "");
    if (j.contains("tools")) {
        for (const auto& jt : j["tools"]) {
            SkillTool tool;
            tool.name = jt.value("name", "");
            tool.description = jt.value("description", "");
            tool.command = jt.value("command", "");
            tool.inputMode = jt.value("inputMode", "stdin");
            tool.systemTool = jt.value("systemTool", false);
            tool.timeoutSecs = jt.value("timeoutSecs", 30);
            if (jt.contains("dockerImage"))
                tool.dockerImage = jt["dockerImage"].get<std::string>();
            if (jt.contains("trustLevel")) {
                std::string tl = jt["trustLevel"].get<std::string>();
                if (tl == "HIGH") tool.trustLevel = TrustLevel::HIGH;
                else if (tl == "LOW") tool.trustLevel = TrustLevel::LOW;
                else tool.trustLevel = TrustLevel::MEDIUM;
            }
            if (jt.contains("aptDependencies"))
                tool.aptDependencies = jt["aptDependencies"].get<std::vector<std::string>>();
            manifest.tools.push_back(tool);
        }
    }
    if (j.contains("prompts")) {
        for (const auto& jp : j["prompts"]) {
            Prompt prompt;
            prompt.name = jp.value("name", "");
            prompt.description = jp.value("description", "");
            // Load prompt text: promptFile takes precedence over inline prompt
            if (jp.contains("promptFile")) {
                std::string relPath = jp["promptFile"].get<std::string>();
                // Resolve relative to the skill.json directory (parent of path)
                std::string baseDir = path;
                auto slash = baseDir.find_last_of("/\\");
                if (slash != std::string::npos)
                    baseDir = baseDir.substr(0, slash);
                std::string fullPath = baseDir + "/" + relPath;
                std::ifstream pf(fullPath);
                if (pf) {
                    std::stringstream ss;
                    ss << pf.rdbuf();
                    prompt.prompt = ss.str();
                } else {
                    std::cerr << "Warning: promptFile not found: " << fullPath << std::endl;
                }
            } else {
                prompt.prompt = jp.value("prompt", "");
            }
            if (jp.contains("dependencies")) {
                prompt.dependencies = jp["dependencies"].get<std::vector<std::string>>();
            }
            if (jp.contains("chain")) {
                prompt.chain = jp["chain"].get<std::vector<std::string>>();
            }
            if (jp.contains("validators")) {
                for (const auto& jv : jp["validators"]) {
                    ValidatorBinding vb;
                    vb.toolName = jv.value("toolName", "");
                    prompt.validators.push_back(std::move(vb));
                }
            }
            manifest.prompts.push_back(prompt);
        }
    }
    return 0;
}

int SkillLoader::addTool(const std::string& component, const SkillTool& tool)
{
    std::string key = xIndexKey(SkillNamespace::LOCAL, component);
    auto it = m_components.find(key);
    if (it == m_components.end()) {
        SkillManifest m;
        m.name = component;
        m.ns = SkillNamespace::LOCAL;
        m.version = "0.1.0";
        m.tools.push_back(tool);
        m_components[key] = m;
        return writeManifest(component, m);
    }
    it->second.tools.push_back(tool);
    return writeManifest(component, it->second);
}

int SkillLoader::addPrompt(const std::string& component, const Prompt& prompt)
{
    std::string key = xIndexKey(SkillNamespace::LOCAL, component);
    auto it = m_components.find(key);
    if (it == m_components.end()) {
        SkillManifest m;
        m.name = component;
        m.ns = SkillNamespace::LOCAL;
        m.version = "0.1.0";
        m.prompts.push_back(prompt);
        m_components[key] = m;
        return writeManifest(component, m);
    }
    it->second.prompts.push_back(prompt);
    return writeManifest(component, it->second);
}

int SkillLoader::updateTool(const std::string& component,
                             const std::string& name,
                             const SkillTool& tool)
{
    std::string key = xIndexKey(SkillNamespace::LOCAL, component);
    auto it = m_components.find(key);
    if (it == m_components.end()) {
        return -1;
    }
    for (auto& t : it->second.tools) {
        if (t.name == name) {
            t = tool;
            return writeManifest(component, it->second);
        }
    }
    return -1;
}

int SkillLoader::removeComponent(const std::string& component)
{
    std::string key = xIndexKey(SkillNamespace::LOCAL, component);
    auto it = m_components.find(key);
    if (it == m_components.end()) {
        return -1;
    }
    if (xIsReadOnly(it->second.ns)) {
        return -1;
    }
    m_components.erase(it);
    std::string dir = m_root + "/local/" + component;
    std::string cmd = "rm -rf " + dir;
    system(cmd.c_str());
    return 0;
}

int SkillLoader::getManifest(SkillNamespace ns,
                              const std::string& component,
                              SkillManifest& manifest) const
{
    std::string key = xIndexKey(ns, component);
    auto it = m_components.find(key);
    if (it == m_components.end()) {
        return -1;
    }
    manifest = it->second;
    return 0;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int SkillLoader::xLoadNamespace(const std::string& dirPath, SkillNamespace ns)
{
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return -1;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        std::string compDir = dirPath + "/" + name;
        struct stat st;
        if (stat(compDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        std::string manifestPath = compDir + "/skill.json";
        SkillManifest manifest;
        if (xParseManifestFile(manifestPath, manifest) != 0) {
            std::cerr << "Warning: skipping " << manifestPath << " (parse error)" << std::endl;
            continue;
        }
        manifest.ns = ns;
        manifest.name = name;
        std::string key = xIndexKey(ns, name);
        m_components[key] = manifest;
        m_nsMap[compDir] = ns;
    }
    closedir(dir);
    return 0;
}

int SkillLoader::xParseManifestFile(const std::string& path, SkillManifest& manifest) const
{
    return readManifest(path, manifest);
}

std::string SkillLoader::xDirForNamespace(SkillNamespace ns) const
{
    switch (ns) {
        case SkillNamespace::SYSTEM: return "system";
        case SkillNamespace::LOCAL:  return "local";
        case SkillNamespace::GITHUB: return "";
    }
    return "";
}

SkillNamespace SkillLoader::xNsForDir(const std::string& dir) const
{
    auto it = m_nsMap.find(m_root + "/" + dir);
    if (it != m_nsMap.end()) {
        return it->second;
    }
    if (dir == "system") return SkillNamespace::SYSTEM;
    if (dir == "local") return SkillNamespace::LOCAL;
    return SkillNamespace::GITHUB;
}

std::string SkillLoader::xIndexKey(SkillNamespace ns, const std::string& component) const
{
    std::string prefix;
    switch (ns) {
        case SkillNamespace::SYSTEM: prefix = "system:"; break;
        case SkillNamespace::LOCAL:  prefix = "local:"; break;
        case SkillNamespace::GITHUB: prefix = "github:"; break;
    }
    return prefix + component;
}

bool SkillLoader::xIsReadOnly(SkillNamespace ns) const
{
    return ns == SkillNamespace::SYSTEM;
}

} // namespace a0::skills
