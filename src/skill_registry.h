#pragma once

#include "agent_interfaces.h"
#include <unordered_map>

class FileSystemSkillRegistry : public SkillRegistry {
public:
    bool loadFromDirectory(const std::string& path) override;
    std::optional<Tool> getTool(const std::string& name) const override;
    std::optional<Prompt> getPrompt(const std::string& name) const override;
    std::vector<std::string> listTools() const override;
    std::vector<std::string> listPrompts() const override;
    bool addTool(const Tool& tool) override;
    bool addPrompt(const Prompt& prompt) override;

private:
    std::unordered_map<std::string, Tool> m_tools;
    std::unordered_map<std::string, Prompt> m_prompts;
    std::string m_basePath;

    int xLoadFilesInDir(const std::string& dirPath, bool readOnly);
};
