#pragma once

#include "agent_interfaces.h"
#include <unordered_map>

class FileSystemComponentRegistry : public ComponentRegistry {
public:
    bool loadFromDirectory(const std::string& path) override;
    std::optional<Tool> getTool(const std::string& name) const override;
    std::optional<Skill> getSkill(const std::string& name) const override;
    std::vector<std::string> listTools() const override;
    std::vector<std::string> listSkills() const override;
    bool addTool(const Tool& tool) override;
    bool addSkill(const Skill& skill) override;

private:
    std::unordered_map<std::string, Tool> m_tools;
    std::unordered_map<std::string, Skill> m_skills;
    std::string m_basePath;
};
