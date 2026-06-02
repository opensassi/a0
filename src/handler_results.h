#pragma once

#include <string>
#include <vector>

namespace a0 {

struct HandlerResult {
    std::string output;
    std::vector<std::string> recommendedTools;
};

}
