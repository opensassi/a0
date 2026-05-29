#pragma once

#include "agent_interfaces.h"
#include <ctime>
#include <string>
#include <unordered_map>

namespace a0 {
namespace docker {

struct ComposeStackInfo {
    std::string networkName;
    time_t lastUsed;
};

class DockerComposeManager : public ComposeManager {
public:
    explicit DockerComposeManager(int idleTimeout);

    std::string startEnvironment(const Skill& skill,
                                  const std::string& skillDirectory) override;
    void stopEnvironment(const Skill& skill) override;
    void markUsed(const Skill& skill) override;

    void setCurrentSkill(const Skill& skill) override;
    std::string getCurrentNetwork() const override;
    void clearCurrentSkill() override;

private:
    int m_idleTimeout;
    std::unordered_map<std::string, ComposeStackInfo> m_stacks;
    std::string m_currentSkillName;
};

} // namespace docker
} // namespace a0