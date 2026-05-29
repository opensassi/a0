#pragma once

#include "skills.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace a0::skills {

class SkillLoader {
public:
    explicit SkillLoader(const std::string& root);

    int loadAll();

    int getTool(const std::string& ns,
                const std::string& component,
                const std::string& toolName,
                SkillTool& tool) const;

    int getPrompt(const std::string& ns,
                  const std::string& component,
                  const std::string& promptName,
                  SkillPrompt& prompt) const;

    std::vector<std::string> listComponents(SkillNamespace ns) const;

    int writeManifest(const std::string& component, const SkillManifest& manifest);

    int readManifest(const std::string& path, SkillManifest& manifest) const;

    int addTool(const std::string& component, const SkillTool& tool);
    int addPrompt(const std::string& component, const SkillPrompt& prompt);
    int updateTool(const std::string& component, const std::string& name, const SkillTool& tool);
    int removeComponent(const std::string& component);
    int getManifest(SkillNamespace ns, const std::string& component, SkillManifest& manifest) const;

private:
    std::string m_root;
    std::unordered_map<std::string, SkillManifest> m_components;
    std::unordered_map<std::string, SkillNamespace> m_nsMap;

    int xLoadNamespace(const std::string& dirPath, SkillNamespace ns);
    int xParseManifestFile(const std::string& path, SkillManifest& manifest) const;
    std::string xDirForNamespace(SkillNamespace ns) const;
    SkillNamespace xNsForDir(const std::string& dir) const;
    std::string xIndexKey(SkillNamespace ns, const std::string& component) const;
    bool xIsReadOnly(SkillNamespace ns) const;
};

} // namespace a0::skills
