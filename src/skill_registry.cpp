#include "skill_registry.h"
#include "trace.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::string stripExt(const std::string& filename) {
    if (endsWith(filename, ".tool.json")) {
        return filename.substr(0, filename.size() - 10);
    }
    if (endsWith(filename, ".prompt.json")) {
        return filename.substr(0, filename.size() - 12);
    }
    auto pos = filename.find_last_of(".");
    if (pos == std::string::npos) return filename;
    return filename.substr(0, pos);
}

bool FileSystemSkillRegistry::loadFromDirectory(const std::string& path) {
    TRACE_LOG("loadFromDirectory(" << path << ")");
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return false;
    }
    m_basePath = path;
    m_tools.clear();
    m_prompts.clear();

    // Walk namespace/<package>/<files>
    for (const auto& nsEntry : fs::directory_iterator(path)) {
        if (!nsEntry.is_directory()) continue;
        std::string ns = nsEntry.path().filename().string();
        bool readOnly = (ns == "system");

        for (const auto& pkgEntry : fs::directory_iterator(nsEntry.path())) {
            if (!pkgEntry.is_directory()) continue;
            xLoadFilesInDir(pkgEntry.path().string(), readOnly);
        }
    }

    return true;
}

int FileSystemSkillRegistry::xLoadFilesInDir(const std::string& dirPath, bool readOnly) {
    (void)readOnly;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();

        try {
            std::ifstream file(entry.path());
            if (!file) continue;
            json j;
            file >> j;

            if (endsWith(filename, ".tool.json")) {
                Tool t;
                t.name = j.value("name", stripExt(filename));
                t.description = j.value("description", "");
                t.command = j.value("command", "");
                t.inputMode = j.value("inputMode", "stdin");
                t.dockerImage = j.value("dockerImage", "");
                std::string trustStr = j.value("trustLevel", "MEDIUM");
                if (trustStr == "HIGH") t.trustLevel = TrustLevel::HIGH;
                else if (trustStr == "LOW") t.trustLevel = TrustLevel::LOW;
                else t.trustLevel = TrustLevel::MEDIUM;
                for (const auto& dep : j["aptDependencies"]) {
                    t.aptDependencies.push_back(dep.get<std::string>());
                }
                m_tools[t.name] = std::move(t);
            } else if (endsWith(filename, ".prompt.json")) {
                Prompt p;
                p.name = j.value("name", stripExt(filename));
                p.description = j.value("description", "");
                p.prompt = j.value("prompt", "");
                for (const auto& dep : j["dependencies"]) {
                    p.dependencies.push_back(dep.get<std::string>());
                }
                for (const auto& v : j["validators"]) {
                    ValidatorBinding vb;
                    vb.toolName = v.value("toolName", "");
                    p.validators.push_back(std::move(vb));
                }
                p.composeFile = j.value("composeFile", "");
                for (const auto& dep : j["aptDependencies"]) {
                    p.aptDependencies.push_back(dep.get<std::string>());
                }
                m_prompts[p.name] = std::move(p);
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: skipping " << filename << ": " << e.what() << std::endl;
        }
    }
    return 0;
}

std::optional<Tool> FileSystemSkillRegistry::getTool(const std::string& name) const {
    TRACE_LOG("getTool(" << name << ")");
    auto it = m_tools.find(name);
    if (it != m_tools.end()) return it->second;
    return std::nullopt;
}

std::optional<Prompt> FileSystemSkillRegistry::getPrompt(const std::string& name) const {
    TRACE_LOG("getPrompt(" << name << ")");
    auto it = m_prompts.find(name);
    if (it != m_prompts.end()) return it->second;
    return std::nullopt;
}

std::vector<std::string> FileSystemSkillRegistry::listTools() const {
    TRACE_LOG("listTools()");
    std::vector<std::string> names;
    names.reserve(m_tools.size());
    for (const auto& [name, _] : m_tools) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> FileSystemSkillRegistry::listPrompts() const {
    TRACE_LOG("listPrompts()");
    std::vector<std::string> names;
    names.reserve(m_prompts.size());
    for (const auto& [name, _] : m_prompts) {
        names.push_back(name);
    }
    return names;
}

bool FileSystemSkillRegistry::addTool(const Tool& tool) {
    TRACE_LOG("addTool(" << tool.name << ")");
    m_tools[tool.name] = tool;
    if (!m_basePath.empty()) {
        // Write to local/<tool.name>/<tool.name>.tool.json
        std::string pkgDir = m_basePath + "/local/" + tool.name;
        fs::create_directories(pkgDir);
        std::string path = pkgDir + "/" + tool.name + ".tool.json";
        std::ofstream file(path);
        if (file) {
            json j;
            j["name"] = tool.name;
            j["description"] = tool.description;
            j["command"] = tool.command;
            j["inputMode"] = tool.inputMode;
            if (!tool.dockerImage.empty()) j["dockerImage"] = tool.dockerImage;
            switch (tool.trustLevel) {
                case TrustLevel::HIGH: j["trustLevel"] = "HIGH"; break;
                case TrustLevel::LOW:  j["trustLevel"] = "LOW"; break;
                default:               j["trustLevel"] = "MEDIUM"; break;
            }
            if (!tool.aptDependencies.empty()) j["aptDependencies"] = tool.aptDependencies;
            file << j.dump(2);
        }
    }
    return true;
}

bool FileSystemSkillRegistry::addPrompt(const Prompt& prompt) {
    TRACE_LOG("addPrompt(" << prompt.name << ")");
    m_prompts[prompt.name] = prompt;
    if (!m_basePath.empty()) {
        // Write to local/<prompt.name>/<prompt.name>.prompt.json
        std::string pkgDir = m_basePath + "/local/" + prompt.name;
        fs::create_directories(pkgDir);
        std::string path = pkgDir + "/" + prompt.name + ".prompt.json";
        std::ofstream file(path);
        if (file) {
            json j;
            j["name"] = prompt.name;
            j["description"] = prompt.description;
            j["prompt"] = prompt.prompt;
            j["dependencies"] = prompt.dependencies;
            if (!prompt.composeFile.empty()) j["composeFile"] = prompt.composeFile;
            if (!prompt.aptDependencies.empty()) j["aptDependencies"] = prompt.aptDependencies;
            file << j.dump(2);
        }
    }
    return true;
}
