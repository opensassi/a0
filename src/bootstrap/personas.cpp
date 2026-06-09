#include "personas.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>

namespace a0::personas {

static std::string xToLower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

PersonaLoader::PersonaLoader(const std::string& root)
    : m_root(root)
{
}

int PersonaLoader::loadAll() {
    m_personas.clear();

    {
        DIR* dir = opendir(m_root.c_str());
        if (!dir) return -1;
        closedir(dir);
    }

    xLoadNamespace(m_root + "/system", PersonaNamespace::SYSTEM);
    xLoadNamespace(m_root + "/local", PersonaNamespace::LOCAL);

    DIR* rootDir = opendir(m_root.c_str());
    if (rootDir) {
        struct dirent* entry;
        while ((entry = readdir(rootDir)) != nullptr) {
            std::string name(entry->d_name);
            if (name.rfind("github_", 0) == 0) {
                struct stat st;
                std::string path = m_root + "/" + name;
                if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    xLoadNamespace(path, PersonaNamespace::GITHUB);
                }
            }
        }
        closedir(rootDir);
    }

    return 0;
}

std::optional<Persona> PersonaLoader::getPersona(const std::string& name) const {
    std::string lower = xToLower(name);
    for (const auto& p : m_personas) {
        if (xToLower(p.manifest.name) == lower) {
            return p;
        }
    }
    return std::nullopt;
}

std::vector<Persona> PersonaLoader::listPersonas() const {
    return m_personas;
}



// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int PersonaLoader::xLoadNamespace(const std::string& dirPath, PersonaNamespace ns) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return -1;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;

        std::string personaDir = dirPath + "/" + name;
        struct stat st;
        if (stat(personaDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }

        auto manifest = xParseManifest(personaDir);
        if (!manifest) {
            std::cerr << "Warning: skipping persona at " << personaDir << " (invalid persona.json)" << std::endl;
            continue;
        }

        manifest->ns = ns;
        manifest->dir = personaDir;

        std::string prompt = xReadPrompt(personaDir, manifest->promptFile);

        Persona persona;
        persona.manifest = std::move(*manifest);
        persona.prompt = std::move(prompt);
        m_personas.push_back(std::move(persona));
    }
    closedir(dir);
    return 0;
}

std::optional<PersonaManifest> PersonaLoader::xParseManifest(const std::string& dir) const {
    std::string path = dir + "/persona.json";
    std::ifstream ifs(path);
    if (!ifs) return std::nullopt;

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (...) {
        return std::nullopt;
    }

    // Validate required fields
    if (!j.contains("name") || !j["name"].is_string()) return std::nullopt;
    if (!j.contains("description") || !j["description"].is_string()) return std::nullopt;
    if (!j.contains("promptFile") || !j["promptFile"].is_string()) return std::nullopt;

    PersonaManifest manifest;
    manifest.name = j["name"].get<std::string>();
    manifest.description = j["description"].get<std::string>();
    manifest.promptFile = j["promptFile"].get<std::string>();

    if (j.contains("skills") && j["skills"].is_array()) {
        for (const auto& s : j["skills"]) {
            if (s.is_string()) manifest.skills.push_back(s.get<std::string>());
        }
    }
    if (j.contains("tools") && j["tools"].is_array()) {
        for (const auto& t : j["tools"]) {
            if (t.is_string()) manifest.tools.push_back(t.get<std::string>());
        }
    }

    return manifest;
}

std::string PersonaLoader::xReadPrompt(const std::string& dir, const std::string& promptFile) const {
    std::string path = dir + "/" + promptFile;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Warning: prompt file not found: " << path << std::endl;
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace a0::personas
