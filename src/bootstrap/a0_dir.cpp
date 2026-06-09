#include "a0_dir.h"
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

namespace a0 {

static bool dirExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool fileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static int mkdirParents(const std::string& path) {
    // Walk each component, mkdir if missing
    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            std::string parent = path.substr(0, i);
            if (!dirExists(parent)) {
                if (::mkdir(parent.c_str(), 0755) != 0 && errno != EEXIST) {
                    return -1;
                }
            }
        }
    }
    if (!dirExists(path)) {
        if (::mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
            return -1;
        }
    }
    return 0;
}

static bool isGitRepository(const std::string& cwd) {
    return dirExists(cwd + "/.git");
}

static bool gitignoreHasA0(const std::string& gitignorePath) {
    std::ifstream f(gitignorePath);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        // Trim trailing whitespace/carriage return
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.pop_back();
        if (line == ".a0" || line == ".a0/") {
            return true;
        }
    }
    return false;
}

static int appendGitignoreEntry(const std::string& gitignorePath) {
    std::ofstream f(gitignorePath, std::ios::app);
    if (!f) return -1;
    // Ensure trailing newline before appending
    f << ".a0/" << std::endl;
    return 0;
}

int ensureA0Dir(const std::string& a0Path, bool requireWorktree) {
    if (a0Path.empty()) return -1;

    bool existed = dirExists(a0Path);

    if (mkdirParents(a0Path) != 0) {
        std::cerr << "a0: failed to create directory " << a0Path
                  << ": " << std::strerror(errno) << std::endl;
        return -1;
    }

    std::string worktreesPath = a0Path + "/worktrees";
    if (requireWorktree) {
        // Resume session — worktrees/ must already exist
        if (!dirExists(worktreesPath)) {
            std::cerr << "a0: fatal: " << worktreesPath
                      << " does not exist. Cannot resume session." << std::endl;
            return -1;
        }
    } else {
        // New session — ensure worktrees/ exists
        mkdirParents(worktreesPath);
    }

    if (existed) return 1;

    // Newly created — check and update .gitignore if inside a git repo
    std::string cwd = ".";
    if (!isGitRepository(cwd)) return 0;

    std::string gitignorePath = cwd + "/.gitignore";
    if (gitignoreHasA0(gitignorePath)) return 0;

    if (appendGitignoreEntry(gitignorePath) != 0) {
        std::cerr << "a0: warning: could not update .gitignore" << std::endl;
    }

    return 0;
}

} // namespace a0
