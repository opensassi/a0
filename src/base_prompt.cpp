#include "base_prompt.h"
#include "skills/skills.h"
#include "agent_interfaces.h"
#include "persistence/build_identity.h"
#include <sys/utsname.h>
#include <unistd.h>
#include <sstream>
#include <string>

namespace a0 {

using a0::persistence::BuildIdentity;

static std::string getBuildHash() {
    static const std::string hash = BuildIdentity::binarySha1();
    return hash;
}

static std::string getOsInfo() {
    static const std::string info = [] {
        struct utsname buf;
        if (uname(&buf) != 0) return std::string("linux");
        std::ostringstream os;
        os << buf.sysname << " " << buf.release << " " << buf.machine;
        return os.str();
    }();
    return info;
}

std::string buildBasePrompt(const skills::SkillManager* skillMgr) {
    (void)skillMgr;
    std::ostringstream prompt;
    prompt << "a0 build " << getBuildHash();
    prompt << " | " << getOsInfo();
    char buf[4096];
    if (::getcwd(buf, sizeof(buf))) {
        prompt << " | cwd: " << buf;
    }
    return prompt.str();
}

} // namespace a0
