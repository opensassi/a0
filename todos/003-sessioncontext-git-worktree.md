# 003-SessionContext-Git-Worktree

## Previous Work

### What Succeeded
- Unified tool dispatch: `SystemToolRegistry` eliminated, all C++ handlers registered directly on `SkillManager` via `registerHandler()`. `executeTool()` handles exact → 2-part alias → wildcard → command tool subprocess.
- Schema generation: `SkillManager::schemas()` generates LLM tool schemas from manifests, filtered by `"default": true` flag in skill.json.
- Startup validation: `SkillManager::missingHandlers()` ensures every `systemTool: true` entry has a registered handler; missing = fatal error listing all.
- All 190 tests pass.

### What Was Tried
- Initial design used `SystemToolExecutor` as a separate pre-init bridge for non-LLM tool execution. Merged into `SkillManager` directly instead — simpler, no new abstraction needed.

### What Remains
Implement `SessionContext` — a top-level data structure initialized at startup that:

1. **Stores the initial CWD** (already implicitly available via `getcwd()`)
2. **Detects git repository** via `SystemToolRegistry::execute("git_detect")` — uses the new system tool handler registered on `SkillManager`
3. **Creates a git worktree** at `.a0/worktrees/a0-session-<uuid8>` with branch `a0/session-<uuid8>` — via `SkillManager::executeTool("system:git:create_worktree", {name, path})`
4. **Changes CWD** to the worktree — all subsequent file operations resolve relative to it
5. **Supplies session-aware Docker container naming** — `containerName("high")` returns `a0-<session-prefix>-high` to prevent cross-worktree conflicts

### Key Technical Details

**New files:**
- `src/session_context.h` / `src/session_context.cpp`

**Interface:**
```cpp
struct GitInfo {
    bool isRepo = false;
    std::string repoRoot, currentBranch, commitHash;
};

class SessionContext {
public:
    SessionContext(const std::string& cwd, const std::string& a0Dir,
                   const std::string& sessionId, int64_t sessionDbId,
                   a0::persistence::PersistenceStore* persistence);

    int init(a0::skills::SkillManager* skillMgr);
    int finalize(a0::skills::SkillManager* skillMgr,
                 const std::string& commitMessage = "");

    const GitInfo& gitInfo() const;
    const std::string& effectiveCwd() const;
    std::string containerName(const std::string& base) const;

private:
    int xRecordToolCall(a0::skills::SkillManager* skillMgr,
                        const std::string& qn, const json& params);
    std::string m_cwd, m_a0Dir, m_sessionId, m_effectiveCwd, m_worktreePath;
    GitInfo m_git;
    a0::persistence::PersistenceStore* m_persistence;
    int64_t m_sessionDbId = 0;
};
```

**xRecordToolCall:** Wraps `SkillManager::executeToolWithMeta()` with persistence recording (`appendMessage` + `appendInvocation`) so init-phase git operations are stored in SQLite as session tool calls.

**System tool handlers needed (add to `skills/system/git/skill.json` + register in `main.cpp`):**
- `detect` — runs `git rev-parse` commands, returns `{"isRepo":true,"repoRoot":"...","branch":"...","commitHash":"..."}`
- `create_worktree` — `git branch a0/session-<name> HEAD && git worktree add <path> a0/session-<name>`
- `remove_worktree` — `git worktree remove <path> && git branch -D a0/session-<name>`

**Startup order change** (Phase 2):
```
1. Parse CLI flags
2. Load env file
3. Resolve API key
4. ensureA0Dir(a0Dir)  ← also creates worktrees/ subdir
5. Generate session UUID
6. Build SqliteStore, register agent, create session
7. Build SubprocessToolRunner, SkillManager, register handlers
8. Build SessionContext(cwd, a0Dir, uuid, sessionDbId, persistence)
9. SessionContext::init(skillMgr)  ← detect git → create worktree → chdir
10. Build remaining AgentStack (Docker, SkillRunner, AgentCore)
11. core.init(skillsDir)
12. REPL loop
```

**Docker container naming:** `ContainerManager` accepts a session prefix string. `SessionContext::containerName("high")` returns `a0-d4a7f2c1-high`. This prevents container name collisions when two agents run in different worktrees.

**Worktree cleanup:** Only removed if `finalize(true, "message")` is called with commit + merge to main. Otherwise left indefinitely for future GC.
