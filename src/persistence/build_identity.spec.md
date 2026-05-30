# BuildIdentity Spec

## 1. Overview

Fingerprints the running a0 binary and the git repository state. Produces a SHA1 hash of the binary and detects git metadata (origin URL, HEAD commit, dirty status) for session tracing.

**Source files:** `src/persistence/build_identity.h/.cpp`

**Dependencies:** POSIX (`sha1sum`, `git`)

## 2. Component Specifications

```cpp
class BuildIdentity {
public:
    /// SHA1 of the running a0 binary via /proc/self/exe.
    static std::string binarySha1();

    /// Detect git metadata from the project root.
    static void detectGit(const std::string& projectDir, BuildFingerprint& fp);
};
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| binarySha1 | Returns 40-char hex string |
| detectGit in git repo | Populates repoUrl, commitHash |
| detectGit outside git repo | Commit hash empty, no crash |
