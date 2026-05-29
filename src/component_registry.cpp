#include "component_registry.h"
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
    if (endsWith(filename, ".skill.json")) {
        return filename.substr(0, filename.size() - 11);
    }
    auto pos = filename.find_last_of(".");
    if (pos == std::string::npos) return filename;
    return filename.substr(0, pos);
}

bool FileSystemComponentRegistry::loadFromDirectory(const std::string& path) {
    TRACE_LOG("loadFromDirectory(" << path << ")");
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return false;
    }
    m_basePath = path;
    m_tools.clear();
    m_skills.clear();

    for (const auto& entry : fs::directory_iterator(path)) {
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
                t.useContainerPool = j.value("useContainerPool", true);
                for (const auto& dep : j["aptDependencies"]) {
                    t.aptDependencies.push_back(dep.get<std::string>());
                }
                m_tools[t.name] = std::move(t);
            } else if (endsWith(filename, ".skill.json")) {
                Skill s;
                s.name = j.value("name", stripExt(filename));
                s.description = j.value("description", "");
                s.prompt = j.value("prompt", "");
                for (const auto& dep : j["dependencies"]) {
                    s.dependencies.push_back(dep.get<std::string>());
                }
                for (const auto& v : j["validators"]) {
                    ValidatorBinding vb;
                    vb.toolName = v.value("toolName", "");
                    s.validators.push_back(std::move(vb));
                }
                s.composeFile = j.value("composeFile", "");
                for (const auto& dep : j["aptDependencies"]) {
                    s.aptDependencies.push_back(dep.get<std::string>());
                }
                m_skills[s.name] = std::move(s);
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: skipping " << filename << ": " << e.what() << std::endl;
        }
    }
    return true;
}

std::optional<Tool> FileSystemComponentRegistry::getTool(const std::string& name) const {
    TRACE_LOG("getTool(" << name << ")");
    auto it = m_tools.find(name);
    if (it != m_tools.end()) return it->second;
    return std::nullopt;
}

std::optional<Skill> FileSystemComponentRegistry::getSkill(const std::string& name) const {
    TRACE_LOG("getSkill(" << name << ")");
    auto it = m_skills.find(name);
    if (it != m_skills.end()) return it->second;
    return std::nullopt;
}

std::vector<std::string> FileSystemComponentRegistry::listTools() const {
    TRACE_LOG("listTools()");
    std::vector<std::string> names;
    names.reserve(m_tools.size());
    for (const auto& [name, _] : m_tools) {
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> FileSystemComponentRegistry::listSkills() const {
    TRACE_LOG("listSkills()");
    std::vector<std::string> names;
    names.reserve(m_skills.size());
    for (const auto& [name, _] : m_skills) {
        names.push_back(name);
    }
    return names;
}

bool FileSystemComponentRegistry::addTool(const Tool& tool) {
    TRACE_LOG("addTool(" << tool.name << ")");
    m_tools[tool.name] = tool;
    if (!m_basePath.empty()) {
        std::string path = m_basePath + "/" + tool.name + ".tool.json";
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
            if (!tool.useContainerPool) j["useContainerPool"] = false;
            if (!tool.aptDependencies.empty()) j["aptDependencies"] = tool.aptDependencies;
            file << j.dump(2);
        }
    }
    return true;
}

bool FileSystemComponentRegistry::addSkill(const Skill& skill) {
    TRACE_LOG("addSkill(" << skill.name << ")");
    m_skills[skill.name] = skill;
    if (!m_basePath.empty()) {
        std::string path = m_basePath + "/" + skill.name + ".skill.json";
        std::ofstream file(path);
        if (file) {
            json j;
            j["name"] = skill.name;
            j["description"] = skill.description;
            j["prompt"] = skill.prompt;
            j["dependencies"] = skill.dependencies;
            if (!skill.composeFile.empty()) j["composeFile"] = skill.composeFile;
            if (!skill.aptDependencies.empty()) j["aptDependencies"] = skill.aptDependencies;
            file << j.dump(2);
        }
    }
    return true;
}
