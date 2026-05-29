#include "docker/container_manager.h"
#include "docker/dependency_installer.h"
#include "docker/docker_cli_wrapper.h"
#include "trace.h"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace a0 {
namespace docker {

DockerContainerManager::DockerContainerManager(int idleTimeout,
                                               int maxIdle,
                                               const std::string& defaultImage)
    : m_idleTimeout(idleTimeout)
    , m_maxIdle(maxIdle)
    , m_defaultImage(defaultImage) {}

std::string DockerContainerManager::poolKeyForTool(const Tool& tool) const {
    switch (tool.trustLevel) {
        case TrustLevel::HIGH:
            return "high_pool";
        case TrustLevel::MEDIUM:
            return "medium_pool";
        case TrustLevel::LOW:
        default:
            return "low_" + tool.name + "_" +
                   (tool.dockerImage.empty() ? m_defaultImage : tool.dockerImage);
    }
}

std::string DockerContainerManager::createContainer(const std::string& poolKey,
                                                     const Tool& tool) {
    std::string image = tool.dockerImage.empty() ? m_defaultImage : tool.dockerImage;
    TRACE_LOG("Creating container poolKey=" << poolKey << " image=" << image);

    // Check if container with this name already exists (from a previous session)
    std::string existingId = DockerCLIWrapper::getContainerId(poolKey);
    if (!existingId.empty()) {
        TRACE_LOG("Reusing existing container: " << poolKey << " (" << existingId << ")");
        DockerCLIWrapper::startContainer(poolKey);
        return existingId;
    }

    try {
        DockerCLIWrapper::pullImage(image);
    } catch (const std::exception& e) {
        TRACE_LOG("Image pull skipped: " << e.what());
    }

    std::string containerId = DockerCLIWrapper::runDetached(
        image, poolKey, "sleep infinity");

    return containerId;
}

void DockerContainerManager::ensureDependencies(
    const std::string& containerId,
    const Tool& tool,
    ContainerPoolEntry& entry) {

    std::vector<std::string> missing;
    for (const auto& pkg : tool.aptDependencies) {
        bool found = false;
        for (const auto& installed : entry.installedDeps) {
            if (installed == pkg) {
                found = true;
                break;
            }
        }
        if (!found) {
            missing.push_back(pkg);
        }
    }

    if (missing.empty()) return;

    DependencyInstaller::install(containerId, missing);
    for (const auto& pkg : missing) {
        entry.installedDeps.push_back(pkg);
    }
}

std::string DockerContainerManager::acquireContainer(const Tool& tool) {
    TRACE_LOG("acquireContainer(" << tool.name << ")");

    auto it = m_pool.find(poolKeyForTool(tool));
    if (it != m_pool.end()) {
        it->second.lastUsed = std::time(nullptr);
        pruneIdleContainers();
        return it->second.containerId;
    }

    std::string poolKey = poolKeyForTool(tool);
    std::string containerId = createContainer(poolKey, tool);

    ContainerPoolEntry entry;
    entry.containerId = containerId;
    entry.image = tool.dockerImage.empty() ? m_defaultImage : tool.dockerImage;
    entry.lastUsed = std::time(nullptr);

    ensureDependencies(containerId, tool, entry);

    m_pool[poolKey] = std::move(entry);
    pruneIdleContainers();
    return containerId;
}

std::string DockerContainerManager::execInContainer(
    const std::string& containerId,
    const std::string& command,
    const std::string& stdinData) {
    TRACE_LOG("execInContainer(" << containerId << ")");

    try {
        return DockerCLIWrapper::execInContainer(containerId, command, stdinData, 30);
    } catch (const std::exception& e) {
        if (std::strstr(e.what(), "timeout") != nullptr) {
            return "ERROR: timeout";
        }
        return "ERROR: " + std::string(e.what());
    }
}

void DockerContainerManager::pruneIdleContainers() {
    TRACE_LOG("pruneIdleContainers()");
    time_t now = std::time(nullptr);

    // Remove expired containers
    std::vector<std::string> toRemove;
    for (const auto& [key, entry] : m_pool) {
        if (now - entry.lastUsed > m_idleTimeout) {
            toRemove.push_back(key);
        }
    }

    for (const auto& key : toRemove) {
        TRACE_LOG("Pruning expired container: " << key);
        try {
            DockerCLIWrapper::stopAndRemove(m_pool[key].containerId);
        } catch (...) {}
        m_pool.erase(key);
    }

    // Enforce max idle limit
    while (static_cast<int>(m_pool.size()) > m_maxIdle) {
        auto oldest = m_pool.begin();
        for (auto it = m_pool.begin(); it != m_pool.end(); ++it) {
            if (it->second.lastUsed < oldest->second.lastUsed) {
                oldest = it;
            }
        }
        TRACE_LOG("Pruning excess container: " << oldest->first);
        try {
            DockerCLIWrapper::stopAndRemove(oldest->second.containerId);
        } catch (...) {}
        m_pool.erase(oldest);
    }
}

} // namespace docker
} // namespace a0