# VersionManager Spec

## 1. Overview

Manages version archives for the Skills sub-module. Stores commit-level snapshots of skill components in `.a0/store/` with reference counting via `lock.json`. Provides archive, restore, release, and garbage collection operations.

**Source files:** `src/skills/version_manager.h/.cpp`

## 2. Component Specifications

```cpp
class VersionManager {
public:
    VersionManager(const std::string& storeRoot, const std::string& skillsRoot);

    int archive(SkillNamespace ns, const std::string& component,
                const std::string& commit, const std::string& version);
    int restore(SkillNamespace ns, const std::string& component,
                const std::string& commit);
    int release(SkillNamespace ns, const std::string& component,
                const std::string& commit);
    int gc(bool dryRun = false);
};
```

## 3. Testing Requirements

| Test | Verification |
|------|-------------|
| archive then restore | Files match originals |
| archive increments refcount | lock.json refcount +1 |
| release decrements refcount | lock.json refcount -1 |
| gc removes unreferenced | Stale directories cleaned |
