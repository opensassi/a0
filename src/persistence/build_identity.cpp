#include "build_identity.h"
#include "../command_runner.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace a0::persistence {

using a0::CommandRunner;

std::string BuildIdentity::binarySha1()
{
    // Stream the binary to sha1sum via pipe — avoids loading into memory
    auto result = CommandRunner::run("sha1sum /proc/self/exe", "", 10);
    if (result.exitCode == 0) {
        // Output: "<hash>  /proc/self/exe"
        auto space = result.stdout.find(' ');
        if (space != std::string::npos) {
            return result.stdout.substr(0, space);
        }
    }
    throw std::runtime_error("failed to compute binary SHA1");
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
