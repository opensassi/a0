# VersionManager Spec

## 1. Overview

Manages version archives for the Skills sub-module. Stores commit-level snapshots of skill components in `.a0/store/` with reference counting via `lock.json`. Provides archive, restore, release, and garbage collection operations.

**Source files:** `src/skills/version_manager.h/.cpp`

**Store layout:**
```
storeRoot/
  lock.json
  system/<commit>/<component>/
  local/<commit>/<component>/
  github/<commit>/<component>/
```

## 2. Component Specifications

```cpp
namespace a0::skills {

struct StoredVersion {
    std::string commitHash;
    std::string version;
    int refcount = 0;
    time_t installedAt = 0;
};

class VersionManager {
public:
    VersionManager(const std::string& storeRoot,
                   const std::string& skillsRoot);

    /// Archive the current active version to the store.
    /// If already archived, increments refcount.
    /// \returns 0 on success, -1 on copy failure.
    int archive(SkillNamespace ns,
                const std::string& component,
                const std::string& commit,
                const std::string& version);

    /// Restore a previously archived version to the active path.
    /// \returns 0 on success, -1 if commit not found.
    int restore(SkillNamespace ns,
                const std::string& component,
                const std::string& commit);

    /// Release a reference. If commit is empty, releases the currently
    /// active version for the component. Decrements refcount (floor 0).
    /// \returns 0 on success, -1 if commit not found.
    int release(SkillNamespace ns,
                const std::string& component,
                const std::string& commit = "");

    /// Garbage collection: remove all entries with refcount <= 0.
    /// When dryRun=true, only reports count without removing.
    /// \returns Number of entries removed (or eligible for removal).
    int gc(bool dryRun = false);

private:
    std::string m_storeRoot;
    std::string m_skillsRoot;
    std::string m_lockPath;
    std::unordered_map<std::string, StoredVersion> m_versions;

    int xLoadLock();
    int xSaveLock();
    std::string xStorePath(SkillNamespace ns,
                           const std::string& commit,
                           const std::string& component) const;
    std::string xVersionKey(SkillNamespace ns,
                            const std::string& component,
                            const std::string& commit) const;
    std::string xActivePath(SkillNamespace ns,
                            const std::string& component) const;
    int xCopyDir(const std::string& src, const std::string& dst);
};

} // namespace a0::skills
```

## 3. Lock File Format (`lock.json`)

```json
{
  "version": 1,
  "versions": {
    "system:component:abc123": {
      "commit": "abc123",
      "version": "v1.0.0",
      "refcount": 2,
      "installedAt": 1717000000
    }
  }
}
```

## 4. Testing Requirements

| Test | Verification |
|------|-------------|
| archive new component | Directory copied to store, refcount=1 |
| archive existing commit | Refcount incremented, no copy |
| restore archived version | Files restored to active path match originals |
| release known commit | Refcount decremented |
| release empty commit (active) | Active version's refcount decremented |
| release unknown commit | Returns -1 |
| gc removes zero-refcount | Stale directories cleaned from store |
| gc with dryRun | Returns count, no files removed |
| gc only removes refcount<=0 | Entries with refcount>0 preserved |
