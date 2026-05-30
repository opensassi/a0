#include "base_prompt.h"
#include "skills/skills.h"
#include "agent_interfaces.h"
#include "persistence/build_identity.h"
#include <sys/utsname.h>
#include <unistd.h>
#include <sstream>
#include <string>
#include <vector>

namespace a0 {

using a0::persistence::BuildIdentity;

// ---------------------------------------------------------------------------
// Build hash — cached via static local
// ---------------------------------------------------------------------------

static std::string getBuildHash() {
    static const std::string hash = BuildIdentity::binarySha1();
    return hash;
}

// ---------------------------------------------------------------------------
// OS info — cached via static local
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Tool parameter descriptions
// ---------------------------------------------------------------------------

struct ToolParam {
    std::string name;
    std::string type;       // string, number, boolean
    bool required;
    std::string defaultVal; // empty if none
};

struct ToolPrompt {
    std::string name;
    std::string brief;
    std::vector<ToolParam> params;

    std::string format() const {
        std::ostringstream out;
        out << "- " << name << ": " << brief;
        if (!params.empty()) {
            out << ". Parameters: ";
            for (size_t i = 0; i < params.size(); ++i) {
                if (i > 0) out << ", ";
                out << params[i].name << " (" << params[i].type;
                out << (params[i].required ? ", required" : ", optional");
                if (!params[i].defaultVal.empty())
                    out << ", default " << params[i].defaultVal;
                out << ")";
            }
        }
        return out.str();
    }
};

static std::vector<ToolPrompt> getSystemToolDefs() {
    return {
        {"bash",
         "Executes a given bash command",
         {{"command", "string", true, ""},
          {"description", "string", true, ""},
          {"timeout", "number", false, "120000"},
          {"workdir", "string", false, ""}}},
        {"read",
         "Read a file or directory from the local filesystem",
         {{"filePath", "string", true, ""},
          {"offset", "number", false, "1"},
          {"limit", "number", false, "2000"}}},
        {"glob",
         "Fast file pattern matching",
         {{"pattern", "string", true, ""},
          {"path", "string", false, ""}}},
        {"grep",
         "Fast content search using regular expressions",
         {{"pattern", "string", true, ""},
          {"path", "string", false, ""},
          {"include", "string", false, ""}}},
        {"edit",
         "Performs exact string replacements in files",
         {{"filePath", "string", true, ""},
          {"oldString", "string", true, ""},
          {"newString", "string", true, ""},
          {"replaceAll", "boolean", false, "false"}}},
        {"write",
         "Writes a file to the local filesystem",
         {{"filePath", "string", true, ""},
          {"content", "string", true, ""}}}
    };
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string buildBasePrompt(const skills::SkillManager* skillMgr) {
    (void)skillMgr;  // Parameter descriptions are currently compiled in;
                     // future: read tool description/params from skill manifests

    std::ostringstream prompt;

    prompt << "You are a0 build " << getBuildHash() << ".\n";
    prompt << "You run in a " << getOsInfo() << " environment.\n";

    // Add CWD
    {
        char buf[4096];
        if (::getcwd(buf, sizeof(buf))) {
            prompt << "Your current working directory is " << buf << ".\n";
        }
    }

    prompt << "Use relative paths from your CWD unless you need an absolute path.\n";
    prompt << "For file discovery, prefer the glob and grep tools over bash find(1).\n";
    prompt << "\n";

    prompt << "The following system tools are always available:\n";
    for (const auto& tool : getSystemToolDefs()) {
        prompt << tool.format() << "\n";
    }

    return prompt.str();
}

} // namespace a0
