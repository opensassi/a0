#pragma once

#include "agent_interfaces.h"
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace a0 {
namespace docker {

struct ComposeStackInfo {
    std::string composeFile;
    std::string networkName;
    time_t lastUsed;
};

class DockerComposeManager : public ComposeManager {
public:
    explicit DockerComposeManager(int idleTimeout);

    std::string startEnvironment(const Prompt& skill,
                                  const std::string& skillDirectory) override;
    void stopEnvironment(const Prompt& skill) override;
    void markUsed(const Prompt& skill) override;

    void setCurrentPrompt(const Prompt& prompt) override;
    std::string getCurrentNetwork() const override;
    void clearCurrentPrompt() override;

    std::string startPersistent(const std::string& name,
                                 const std::string& composeFile,
                                 const std::string& skillDirectory) override;
    void stopPersistent(const std::string& name) override;
    bool isPersistent(const std::string& name) const override;

private:
    int m_idleTimeout;
    std::unordered_map<std::string, ComposeStackInfo> m_stacks;
    std::unordered_set<std::string> m_persistentStacks;
    std::string m_currentPromptName;
};

} // namespace docker
} // namespace a0