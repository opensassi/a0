#include "base_prompt.h"
#include "agent_interfaces.h"
#include "persistence/build_identity.h"
#include <sys/utsname.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>
#include <cstdlib>
#include <vector>

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

static std::string loadTemplate() {
    // Try in order: CWD, parent-of-CWD (for build/ dir), A0_DIR, then raw relative path
    std::vector<std::string> candidates;
    char cwd[4096];
    if (::getcwd(cwd, sizeof(cwd))) {
        candidates.push_back(std::string(cwd) + "/prompts/base.md");
        // Also try ../prompts/base.md (build dir case)
        std::string parent(cwd);
        auto slash = parent.rfind('/');
        if (slash != std::string::npos) {
            parent = parent.substr(0, slash);
            candidates.push_back(parent + "/prompts/base.md");
        }
    }
    const char* a0Dir = std::getenv("A0_DIR");
    if (a0Dir) {
        candidates.push_back(std::string(a0Dir) + "/prompts/base.md");
    }
    candidates.push_back("prompts/base.md");

    for (const auto& path : candidates) {
        std::ifstream f(path);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    return "You are a0 build {{BUILD_HASH}}.\nEnvironment: {{OS_INFO}}\nCWD: {{CWD}}\n";
}

static std::string substitute(std::string tmpl) {
    static const std::string buildHash = getBuildHash();
    static const std::string osInfo = getOsInfo();

    auto replace = [&](const std::string& key, const std::string& val) {
        size_t pos = 0;
        while ((pos = tmpl.find(key, pos)) != std::string::npos) {
            tmpl.replace(pos, key.size(), val);
            pos += val.size();
        }
    };

    replace("{{BUILD_HASH}}", buildHash);
    replace("{{OS_INFO}}", osInfo);

    char cwd[4096];
    if (::getcwd(cwd, sizeof(cwd))) {
        replace("{{CWD}}", cwd);
    }

    return tmpl;
}

std::string buildBasePrompt(const skills::SkillManager* skillMgr) {
    (void)skillMgr;
    static const std::string prompt = substitute(loadTemplate());
    return prompt;
}

} // namespace a0
