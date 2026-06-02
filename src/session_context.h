#pragma once

#include <memory>
#include <string>
#include "nlohmann/json.hpp"

namespace a0::skills { class SkillManager; }
namespace a0::persistence {
    struct SessionContextRow;
    class PersistenceStore;
}

namespace a0 {

struct GitInfo {
    bool isRepo = false;
    std::string repoRoot;
    std::string currentBranch;
    std::string commitHash;
};

class SessionContext {
public:
    /// Create a new session context: detect git, create worktree, chdir.
    SessionContext(const std::string& cwd, const std::string& a0Dir,
                   const std::string& sessionId, int64_t sessionDbId,
                   a0::persistence::PersistenceStore* persistence = nullptr);

    /// Run git detection and worktree creation.
    int init(a0::skills::SkillManager* skillMgr);

    /// Load an existing session context from the database.
    /// Returns null if no context record exists.
    static std::unique_ptr<SessionContext> loadFromDb(
        int64_t sessionDbId,
        const std::string& a0Dir,
        a0::persistence::PersistenceStore* persistence);

    /// Restore a loaded session context: chdir to worktree, re-detect for vars.
    int restore(a0::skills::SkillManager* skillMgr);

    const GitInfo& gitInfo() const { return m_git; }
    const std::string& originalCwd() const { return m_cwd; }
    const std::string& worktreePath() const { return m_worktreePath; }
    std::string containerName(const std::string& base) const;

private:
    int xDetectGit(a0::skills::SkillManager* skillMgr, int& seq);
    int xCreateWorktree(a0::skills::SkillManager* skillMgr, int& seq);
    int xSaveToDb();

    std::string m_cwd;
    std::string m_a0Dir;
    std::string m_sessionId;
    std::string m_sessionPrefix;
    std::string m_effectiveCwd;
    std::string m_worktreePath;
    GitInfo m_git;
    bool m_hasWorktree = false;
    a0::persistence::PersistenceStore* m_persistence = nullptr;
    int64_t m_sessionDbId = 0;
};

} // namespace a0
