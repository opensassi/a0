#pragma once

#include "shared/agent_interfaces.h"
#include <string>

namespace a0 {
namespace docker {

class DockerToolRunnerImpl : public DockerToolRunner {
public:
    DockerToolRunnerImpl(ContainerManager* containerManager,
                         ComposeManager* composeManager,
                         bool poolEnabled = true);

    json run(const Tool& tool, const json& params) override;
    a0::StreamHandle runStreaming(const Tool& tool,
                                   const json& params,
                                   a0::StreamCallback onChunk) override;

private:
    std::string buildCommand(const Tool& tool,
                              const json& params,
                              std::string& outStdin) const;
    std::string runEphemeral(const Tool& tool,
                              const std::string& command,
                              const std::string& stdinData) const;

    ContainerManager* m_containerManager;
    ComposeManager* m_composeManager;
    bool m_poolEnabled;
};

} // namespace docker
} // namespace a0