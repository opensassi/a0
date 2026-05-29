#include "docker/compose_manager.h"
#include "docker/docker_cli_wrapper.h"
#include "trace.h"

namespace a0 {
namespace docker {

DockerComposeManager::DockerComposeManager(int idleTimeout)
    : m_idleTimeout(idleTimeout) {}

std::string DockerComposeManager::startEnvironment(
    const Skill& skill,
    const std::string& skillDirectory) {
    TRACE_LOG("startEnvironment(" << skill.name << ")");

    if (skill.composeFile.empty()) {
        return "";
    }

    auto it = m_stacks.find(skill.name);
    if (it != m_stacks.end()) {
        it->second.lastUsed = std::time(nullptr);
        return it->second.networkName;
    }

    try {
        DockerCLIWrapper::composeUp(skill.composeFile, skillDirectory);
    } catch (const std::exception& e) {
        TRACE_LOG("composeUp failed: " << e.what());
        return "";
    }

    std::string networkName =
        DockerCLIWrapper::getNetworkName(skill.composeFile, skillDirectory);

    ComposeStackInfo info;
    info.networkName = networkName;
    info.lastUsed = std::time(nullptr);
    m_stacks[skill.name] = std::move(info);

    return networkName;
}

void DockerComposeManager::stopEnvironment(const Skill& skill) {
    TRACE_LOG("stopEnvironment(" << skill.name << ")");
    auto it = m_stacks.find(skill.name);
    if (it == m_stacks.end()) return;

    try {
        DockerCLIWrapper::composeDown(skill.composeFile, "");
    } catch (const std::exception& e) {
        TRACE_LOG("composeDown failed: " << e.what());
    }
    m_stacks.erase(it);
}

void DockerComposeManager::markUsed(const Skill& skill) {
    auto it = m_stacks.find(skill.name);
    if (it != m_stacks.end()) {
        it->second.lastUsed = std::time(nullptr);
    }
}

void DockerComposeManager::setCurrentSkill(const Skill& skill) {
    m_currentSkillName = skill.name;
}

std::string DockerComposeManager::getCurrentNetwork() const {
    auto it = m_stacks.find(m_currentSkillName);
    if (it != m_stacks.end()) {
        return it->second.networkName;
    }
    return "";
}

void DockerComposeManager::clearCurrentSkill() {
    m_currentSkillName.clear();
}

} // namespace docker
} // namespace a0