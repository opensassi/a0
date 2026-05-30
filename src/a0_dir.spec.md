# A0Dir Spec

## 1. Overview

Ensures the `.a0/` runtime directory exists at startup. Creates the directory and any missing parents. On first creation, if the CWD is a git repository, appends `.a0/` to `.gitignore` to prevent committing runtime state.

**Source files:** `src/a0_dir.h/.cpp`

**Dependencies:** POSIX (`mkdir`, `stat`), `git` command

## 2. Component Specifications

```cpp
namespace a0 {

/// \param a0Path  Path to the .a0/ directory (e.g. "./.a0").
/// \retval 0  Directory was newly created.
/// \retval 1  Directory already existed.
/// \retval -1 Failed to create directory.
int ensureA0Dir(const std::string& a0Path);

} // namespace a0
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| Directory does not exist | Created, returns 0 |
| Directory already exists | Returns 1, no changes |
| Path with parent dirs | Parents created as needed |
| Git repo on first creation | `.a0/` appended to `.gitignore` |
