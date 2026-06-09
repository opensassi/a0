#include "docker/dependency_installer.h"
#include "docker/docker_cli_wrapper.h"
#include "shared/trace.h"
#include <sstream>
#include <stdexcept>

namespace a0 {
namespace docker {

void DependencyInstaller::install(const std::string& containerId,
                                   const std::vector<std::string>& packages) {
    if (packages.empty()) return;

    TRACE_LOG("DependencyInstaller::install(" << containerId << ", "
              << packages.size() << " packages)");

    std::ostringstream cmd;
    cmd << "DEBIAN_FRONTEND=noninteractive apt-get update -qq && "
        << "DEBIAN_FRONTEND=noninteractive apt-get install -y -qq";
    for (const auto& pkg : packages) {
        cmd << " " << pkg;
    }

    try {
        DockerCLIWrapper::execInContainer(containerId, cmd.str(), "", 120);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Dependency installation failed: ") + e.what());
    }
}

} // namespace docker
} // namespace a0