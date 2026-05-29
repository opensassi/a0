#include "docker/compose_manager.h"
#include "docker/docker_cli_wrapper.h"
#include "trace.h"

namespace a0 {
namespace docker {

DockerComposeManager::DockerComposeManager(int idleTimeout)
    : m_idleTimeout(idleTimeout) {}

std::string DockerComposeManager::startEnvironment(
    const Prompt& prompt,
    const std::string& skillDirectory) {
    TRACE_LOG("startEnvironment(" << prompt.name << ")");

    if (prompt.composeFile.empty()) {
        return "";
    }

    auto it = m_stacks.find(prompt.name);
    if (it != m_stacks.end()) {
        it->second.lastUsed = std::time(nullptr);
        return it->second.networkName;
    }

    try {
        DockerCLIWrapper::composeUp(prompt.composeFile, skillDirectory);
    } catch (const std::exception& e) {
        TRACE_LOG("composeUp failed: " << e.what());
        return "";
    }

    std::string networkName =
        DockerCLIWrapper::getNetworkName(prompt.composeFile, skillDirectory);

    ComposeStackInfo info;
    info.networkName = networkName;
    info.lastUsed = std::time(nullptr);
    m_stacks[prompt.name] = std::move(info);

    return networkName;
}

void DockerComposeManager::stopEnvironment(const Prompt& prompt) {
    TRACE_LOG("stopEnvironment(" << prompt.name << ")");
    auto it = m_stacks.find(prompt.name);
    if (it == m_stacks.end()) return;

    try {
        DockerCLIWrapper::composeDown(prompt.composeFile, "");
    } catch (const std::exception& e) {
        TRACE_LOG("composeDown failed: " << e.what());
    }
    m_stacks.erase(it);
}

void DockerComposeManager::markUsed(const Prompt& prompt) {
    auto it = m_stacks.find(prompt.name);
    if (it != m_stacks.end()) {
        it->second.lastUsed = std::time(nullptr);
    }
}

void DockerComposeManager::setCurrentPrompt(const Prompt& prompt) {
    m_currentPromptName = prompt.name;
}

std::string DockerComposeManager::getCurrentNetwork() const {
    auto it = m_stacks.find(m_currentPromptName);
    if (it != m_stacks.end()) {
        return it->second.networkName;
    }
    return "";
}

void DockerComposeManager::clearCurrentPrompt() {
    m_currentPromptName.clear();
}

} // namespace docker
} // namespace a0