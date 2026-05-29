#pragma once

#include <string>
#include <vector>

namespace a0 {
namespace docker {

class DockerCLIWrapper {
public:
    static std::string runDetached(const std::string& image,
                                    const std::string& name,
                                    const std::string& command);
    static std::string execInContainer(const std::string& containerId,
                                        const std::string& command,
                                        const std::string& stdinData = "",
                                        int timeoutSecs = 30);
    static void stopAndRemove(const std::string& containerId);
    static void pullImage(const std::string& image);
    static void composeUp(const std::string& composeFile,
                          const std::string& projectDir);
    static void composeDown(const std::string& composeFile,
                            const std::string& projectDir);
    static std::string getNetworkName(const std::string& composeFile,
                                       const std::string& projectDir);
};

} // namespace docker
} // namespace a0