# Todo: Deterministic Version Alignment for External a0 Repo

**Created:** 2026-06-03  
**Session:** Skill system implementation — external repo clone + Playwright E2E  
**Priority:** medium  

## Context

The agent clones `https://github.com/opensassi/a0` into `.a0/external/a0` at startup (via `AgentCore::init()` with `--external-repo`). Currently it always checks out `main` HEAD:

```cpp
json cloneParams = {
    {"args", json::array({
        "clone", "--depth", "1", "-b", "main",
        m_externalRepoUrl, externalDir
    })}
};
m_skillMgr->executeTool("system-git-clone", cloneParams);
```

This works for development but creates a mismatch risk: the running binary may have been built from a different commit than what's cloned. The binary's tool scripts (`scripts/browser.sh`, `scripts/playwright-bridge.js`) might not match the binary's expected version.

## Needed

A mechanism to embed the build commit hash and use it to check out the matching source. Sequencing issue: `cmake` runs before `git commit`, so `git rev-parse HEAD` at build time captures the *previous* commit.

**Approach (previously discussed):** Capture `origin/main` hash at build time since it exists on the remote:

```cmake
execute_process(COMMAND git rev-parse origin/main
    OUTPUT_VARIABLE A0_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
target_compile_definitions(a0_lib PRIVATE
    A0_COMMIT_HASH="${A0_COMMIT_HASH}")
```

At clone time:
```bash
git checkout "$A0_COMMIT_HASH"
```

This works because `origin/main` is already pushed hash known to the remote.

**Fallback:** If `origin/main` doesn't exist (no remote), fall back to `HEAD`.

## Files to modify

| File | Change |
|------|--------|
| `CMakeLists.txt` | Capture `origin/main` hash, pass as `A0_COMMIT_HASH` compile definition |
| `src/build_id.h` | **New** — expose `a0::BUILD_COMMIT_HASH` constant |
| `src/agent_core.cpp` | After clone, run `git checkout <A0_COMMIT_HASH>` via git skill; on failure, stay on main |
| `src/main.cpp` | Optionally remove `--external-repo` flag or keep as override |
| `test/unit/test_external_a0.cpp` | Add test for commit checkout logic |

## Dependencies

- The git `system-git-*` wildcard handler must support `checkout` and `rev-parse` — already wired via `xGitCommand()` in `system_handlers.cpp`
