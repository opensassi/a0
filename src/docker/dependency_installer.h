#pragma once

#include <string>
#include <vector>

namespace a0 {
namespace docker {

class DependencyInstaller {
public:
    static void install(const std::string& containerId,
                        const std::vector<std::string>& packages);
};

} // namespace docker
} // namespace a0