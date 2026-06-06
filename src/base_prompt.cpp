#include "base_prompt.h"
#include "personas.h"
#include "agent_interfaces.h"
#include "persistence/build_identity.h"
#include <sys/utsname.h>
#include <unistd.h>
#include <sstream>
#include <string>
#include <regex>
#include <cstdlib>

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

std::string buildBasePrompt(const skills::SkillManager* skillMgr,
                             const std::string& personaName) {
    (void)skillMgr;
    std::string name = personaName.empty() ? "software-engineer" : personaName;
    personas::PersonaLoader loader;
    if (loader.loadAll() != 0) {
        return "ERROR: personas/ directory not found";
    }
    auto persona = loader.getPersona(name);
    if (!persona) {
        return "ERROR: persona \"" + name + "\" not found";
    }
    return substitute(persona->prompt);
}

} // namespace a0
