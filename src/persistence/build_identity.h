#pragma once

#include "persistence_store.h"
#include <string>

namespace a0::persistence {

class BuildIdentity {
public:
    /// SHA1 of the running a0 binary.
    static std::string binarySha1();

    /// Detect git metadata from the project root.
    static void detectGit(const std::string& projectDir, BuildFingerprint& fp);
};

} // namespace a0::persistence
