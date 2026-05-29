#pragma once

#include "agent_interfaces.h"
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace a0 {
namespace docker {

struct ContainerPoolEntry {
    std::string containerId;
    std::string image;
    time_t lastUsed;
    std::vector<std::string> installedDeps;
};

class DockerContainerManager : public ContainerManager {
public:
    DockerContainerManager(int idleTimeout,
                           int maxIdle,
                           const std::string& defaultImage);

    std::string acquireContainer(const Tool& tool) override;
    std::string execInContainer(const std::string& containerId,
                                 const std::string& command,
                                 const std::string& stdinData = "") override;
    void pruneIdleContainers() override;

private:
    std::string poolKeyForTool(const Tool& tool) const;
    std::string createContainer(const std::string& poolKey,
                                 const Tool& tool);
    void ensureDependencies(const std::string& containerId,
                             const Tool& tool,
                             ContainerPoolEntry& entry);

    int m_idleTimeout;
    int m_maxIdle;
    std::string m_defaultImage;
    std::unordered_map<std::string, ContainerPoolEntry> m_pool;
};

} // namespace docker
} // namespace a0