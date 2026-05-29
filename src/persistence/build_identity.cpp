#include "build_identity.h"
#include "../command_runner.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace a0::persistence {

using a0::CommandRunner;

static std::string sha1OfStream(std::istream& is)
{
    // Simple SHA1 using openssl CLI as a fallback
    // In production, link against a SHA library
    std::string content((std::istreambuf_iterator<char>(is)),
                         std::istreambuf_iterator<char>());

    // Use sha1sum command
    auto result = CommandRunner::run(
        "sha1sum", content, 10);
    if (result.exitCode == 0) {
        // Output: "<hash>  -"
        auto space = result.stdout.find(' ');
        if (space != std::string::npos) {
            return result.stdout.substr(0, space);
        }
    }
    return "";
}

std::string BuildIdentity::binarySha1()
{
    std::ifstream self("/proc/self/exe", std::ios::binary);
    if (!self) {
        return "";
    }
    return sha1OfStream(self);
}

void BuildIdentity::detectGit(const std::string& projectDir, BuildFingerprint& fp)
{
    // Repo URL
    auto url = CommandRunner::run(
        "git config --get remote.origin.url", "", 5);
    if (url.exitCode == 0) {
        while (!url.stdout.empty() && url.stdout.back() == '\n')
            url.stdout.pop_back();
        fp.repoUrl = url.stdout;
    }

    // Commit hash
    auto hash = CommandRunner::run(
        "git rev-parse HEAD", "", 5);
    if (hash.exitCode == 0) {
        while (!hash.stdout.empty() && hash.stdout.back() == '\n')
            hash.stdout.pop_back();
        fp.commitHash = hash.stdout;
    }

    // Dirty hash (hash of git diff)
    auto diff = CommandRunner::run(
        "git diff HEAD", "", 5);
    if (diff.exitCode == 0 && !diff.stdout.empty()) {
        auto dirtyHash = CommandRunner::run(
            "sha1sum", diff.stdout, 10);
        if (dirtyHash.exitCode == 0) {
            auto space = dirtyHash.stdout.find(' ');
            if (space != std::string::npos) {
                fp.dirtyHash = dirtyHash.stdout.substr(0, space);
            }
        }
    }
}

} // namespace a0::persistence
