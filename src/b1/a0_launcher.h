#pragma once

#include <string>

namespace a0::b1 {

class A0Launcher {
public:
    explicit A0Launcher(const std::string& a0Binary);

    int runSkill(const std::string& skill,
                 const std::string& params,
                 std::string& result,
                 int timeoutSeconds = 300);

private:
    std::string m_a0Binary;
};

} // namespace a0::b1
