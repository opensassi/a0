#include "session_context.h"
#include "skills/skills.h"
#include "persistence/persistence_store.h"
#include <unistd.h>
#include <sys/stat.h>
#include <iostream>
#include <cstring>

namespace a0 {

SessionContext::SessionContext(const std::string& cwd,
                                const std::string& a0Dir,
                                const std::string& sessionId,
                                int64_t sessionDbId,
                                a0::persistence::PersistenceStore* persistence)
    : m_cwd(cwd)
    , m_a0Dir(a0Dir)
    , m_sessionId(sessionId)
    , m_sessionPrefix(sessionId.substr(0, 8))
    , m_effectiveCwd(cwd)
    , m_persistence(persistence)
    , m_sessionDbId(sessionDbId)
{
}

int SessionContext::init(a0::skills::SkillManager* skillMgr) {
    if (!skillMgr) return -1;

    int seq = 0;

    int rc = xDetectGit(skillMgr, seq);
    if (rc != 0) {
        m_git.isRepo = false;
        m_effectiveCwd = m_cwd;
        return 0;
    }

    rc = xCreateWorktree(skillMgr, seq);
    if (rc != 0) {
        std::cerr << "a0: session: worktree creation failed, continuing in CWD" << std::endl;
        m_effectiveCwd = m_cwd;
        return 0;
    }

    m_hasWorktree = true;
    m_effectiveCwd = m_worktreePath;

    if (::chdir(m_worktreePath.c_str()) != 0) {
        std::cerr << "a0: session: chdir to worktree failed: "
                  << strerror(errno) << std::endl;
        m_effectiveCwd = m_cwd;
        return -1;
    }

    // Persist session context to DB
    xSaveToDb();

    std::cerr << "a0: session: worktree active at " << m_worktreePath << std::endl;
    return 0;
}

std::unique_ptr<SessionContext> SessionContext::loadFromDb(
    int64_t sessionDbId,
    const std::string& a0Dir,
    a0::persistence::PersistenceStore* persistence)
{
    if (!persistence || sessionDbId <= 0) return nullptr;

    auto row = persistence->loadSessionContext(sessionDbId);
    if (row.worktreePath.empty()) return nullptr;

    std::string sessionUuid = row.sessionUuid;
    auto ctx = std::make_unique<SessionContext>(
        row.originalCwd, a0Dir, sessionUuid, sessionDbId, persistence);
    ctx->m_worktreePath = row.worktreePath;
    ctx->m_git.isRepo = !row.gitRepoRoot.empty();
    ctx->m_git.repoRoot = row.gitRepoRoot;
    ctx->m_git.currentBranch = row.gitBranch;
    ctx->m_git.commitHash = row.gitCommit;
    return ctx;
}

int SessionContext::restore(a0::skills::SkillManager* skillMgr) {
    (void)skillMgr;

    if (m_worktreePath.empty()) return -1;

    // Check if worktree still exists
    struct stat st;
    if (::stat(m_worktreePath.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        std::cerr << "a0: session: worktree missing at " << m_worktreePath << std::endl;
        return -1;
    }

    if (::chdir(m_worktreePath.c_str()) != 0) {
        std::cerr << "a0: session: chdir to worktree failed: "
                  << strerror(errno) << std::endl;
        return -1;
    }

    m_effectiveCwd = m_worktreePath;
    m_hasWorktree = true;

    // Re-detect git for fresh vars
    int seq = 0;
    xDetectGit(skillMgr, seq);

    std::cerr << "a0: session: restored worktree at " << m_worktreePath << std::endl;
    return 0;
}

std::string SessionContext::containerName(const std::string& base) const {
    return "a0-" + m_sessionPrefix + "-" + base;
}

int SessionContext::xDetectGit(a0::skills::SkillManager* skillMgr, int& seq) {
    auto r1 = skillMgr->executeToolWithMeta("system:git:rev-parse",
        {{"args", {"--is-inside-work-tree"}}}, &seq, "", 0);
    if (r1.output.find("true") == std::string::npos) {
        return -1;
    }

    m_git.isRepo = true;

    auto r2 = skillMgr->executeToolWithMeta("system:git:rev-parse",
        {{"args", {"--show-toplevel"}}}, &seq, "", 0);
    m_git.repoRoot = r2.output;
    while (!m_git.repoRoot.empty() &&
           (m_git.repoRoot.back() == '\n' || m_git.repoRoot.back() == ' '))
        m_git.repoRoot.pop_back();

    auto r3 = skillMgr->executeToolWithMeta("system:git:rev-parse",
        {{"args", {"--abbrev-ref", "HEAD"}}}, &seq, "", 0);
    m_git.currentBranch = r3.output;
    while (!m_git.currentBranch.empty() &&
           (m_git.currentBranch.back() == '\n' || m_git.currentBranch.back() == ' '))
        m_git.currentBranch.pop_back();

    auto r4 = skillMgr->executeToolWithMeta("system:git:rev-parse",
        {{"args", {"HEAD"}}}, &seq, "", 0);
    m_git.commitHash = r4.output;
    while (!m_git.commitHash.empty() &&
           (m_git.commitHash.back() == '\n' || m_git.commitHash.back() == ' '))
        m_git.commitHash.pop_back();

    return 0;
}

int SessionContext::xCreateWorktree(a0::skills::SkillManager* skillMgr, int& seq) {
    std::string sessionBranch = "a0/session-" + m_sessionPrefix;
    m_worktreePath = m_a0Dir + "/worktrees/a0-session-" + m_sessionPrefix;

    auto r = skillMgr->executeToolWithMeta("system:git:worktree",
        {{"args", {"add", "-b", sessionBranch, m_worktreePath, "HEAD"}}},
        &seq, "", 0);

    if (r.output.rfind("error:", 0) == 0 || r.output.rfind("ERROR:", 0) == 0) {
        return -1;
    }
    return 0;
}

int SessionContext::xSaveToDb() {
    if (!m_persistence) return -1;
    if (m_sessionDbId <= 0 && !m_sessionId.empty()) {
        m_sessionDbId = m_persistence->findSessionByUuid(m_sessionId);
    }
    if (m_sessionDbId <= 0) return -1;

    a0::persistence::SessionContextRow row;
    row.sessionId = m_sessionDbId;
    row.sessionUuid = m_sessionId;
    row.originalCwd = m_cwd;
    row.worktreePath = m_worktreePath;
    row.gitRepoRoot = m_git.repoRoot;
    row.gitBranch = m_git.currentBranch;
    row.gitCommit = m_git.commitHash;
    return m_persistence->saveSessionContext(row);
}

} // namespace a0
