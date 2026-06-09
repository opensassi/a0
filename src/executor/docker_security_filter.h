#pragma once

#include <string>
#include <vector>

namespace a0 {

/// Prevents user-facing docker tools from modifying system-managed sandbox containers.
class DockerSecurityFilter {
public:
    DockerSecurityFilter();

    /// Register a container name/ID as protected.
    void protectContainer(const std::string& nameOrId);

    /// Returns false if the operation targets a protected container.
    bool canAccess(const std::string& containerNameOrId) const;

    /// Returns false if any container in the list is protected.
    bool canAccessAll(const std::vector<std::string>& containers) const;

private:
    std::vector<std::string> m_protected;
    bool xMatches(const std::string& target, const std::string& pattern) const;
};

} // namespace a0
