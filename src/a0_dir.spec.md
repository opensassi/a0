# A0Dir Spec

## 1. Overview

Ensures the `.a0/` runtime directory exists at startup. Creates the directory and any missing parents. On first creation, if the CWD is a git repository, appends `.a0/` to `.gitignore` to prevent committing runtime state.

**Source files:** `src/a0_dir.h/.cpp`

**Dependencies:** POSIX (`mkdir`, `stat`), `git` command

## 2. Component Specifications

```cpp
namespace a0 {

/// Ensure the .a0/ directory exists at @p a0Path.
/// Creates it (and parent dirs) if missing.
/// On first creation, if the CWD is a git repository, appends ".a0/" to .gitignore.
///
/// \param a0Path         Path to the .a0/ directory (e.g. "./.a0").
/// \param requireWorktree  If true, verify worktrees/ subdir exists (for resume).
///                         If false, create worktrees/ subdir if missing.
/// \retval 0  Directory was newly created (gitignore may have been updated).
/// \retval 1  Directory already existed.
/// \retval -1 Failed to create directory or required subdir missing.
int ensureA0Dir(const std::string& a0Path, bool requireWorktree = false);

} // namespace a0
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| Directory does not exist | Created, returns 0 |
| Directory already exists | Returns 1, no changes |
| Path with parent dirs | Parents created as needed |
| Git repo on first creation | `.a0/` appended to `.gitignore` |
| requireWorktree=true, worktrees/ missing | Returns -1, error message printed |
| requireWorktree=false, worktrees/ missing | Created automatically |
| requireWorktree=true, worktrees/ exists | Returns 0/1, normal behaviour |
