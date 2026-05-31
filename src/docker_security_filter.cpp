#include "docker_security_filter.h"
#include <algorithm>

namespace a0 {

DockerSecurityFilter::DockerSecurityFilter()
    : m_protected()
{
}

void DockerSecurityFilter::protectContainer(const std::string& nameOrId) {
    if (!nameOrId.empty()) {
        m_protected.push_back(nameOrId);
    }
}

bool DockerSecurityFilter::xMatches(const std::string& target, const std::string& pattern) const {
    // Exact match
    if (target == pattern) return true;
    // Prefix match (pattern is a prefix of target)
    if (target.rfind(pattern, 0) == 0) return true;
    return false;
}

bool DockerSecurityFilter::canAccess(const std::string& containerNameOrId) const {
    for (const auto& p : m_protected) {
        if (xMatches(containerNameOrId, p)) return false;
    }
    return true;
}

bool DockerSecurityFilter::canAccessAll(const std::vector<std::string>& containers) const {
    for (const auto& c : containers) {
        if (!canAccess(c)) return false;
    }
    return true;
}

} // namespace a0
