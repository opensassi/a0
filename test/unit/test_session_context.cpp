#include "session_context.h"
#include <gtest/gtest.h>
#include <string>

using namespace a0;

// ---------------------------------------------------------------------------
// ContainerName
// ---------------------------------------------------------------------------

TEST(SessionContextTest, ContainerName_ReturnsPrefixed) {
    SessionContext ctx("/tmp", "/tmp/.a0", "abcdef1234567890abcdef1234567890", 0);
    EXPECT_EQ(ctx.containerName("high"), "a0-abcdef12-high");
    EXPECT_EQ(ctx.containerName("medium"), "a0-abcdef12-medium");
}

TEST(SessionContextTest, ContainerName_EmptyBase) {
    SessionContext ctx("/tmp", "/tmp/.a0", "abcdef1234567890abcdef1234567890", 0);
    EXPECT_EQ(ctx.containerName(""), "a0-abcdef12-");
}

TEST(SessionContextTest, ContainerName_Consistent) {
    SessionContext ctx("/tmp", "/tmp/.a0", "deadbeef1234567890abcdef12345678", 0);
    EXPECT_EQ(ctx.containerName("high"), "a0-deadbeef-high");
    EXPECT_EQ(ctx.containerName("high"), "a0-deadbeef-high");
}

// ---------------------------------------------------------------------------
// originalCwd
// ---------------------------------------------------------------------------

TEST(SessionContextTest, OriginalCwd_Stored) {
    SessionContext ctx("/home/user/project", "/home/user/project/.a0",
                       "abc123456789012345678901234567890", 0);
    EXPECT_EQ(ctx.originalCwd(), "/home/user/project");
}

// ---------------------------------------------------------------------------
// GitInfo defaults
// ---------------------------------------------------------------------------

TEST(SessionContextTest, DefaultGitInfo_NotARepo) {
    SessionContext ctx("/tmp", "/tmp/.a0", "abc123456789012345678901234567890", 0);
    EXPECT_FALSE(ctx.gitInfo().isRepo);
    EXPECT_TRUE(ctx.gitInfo().repoRoot.empty());
    EXPECT_TRUE(ctx.gitInfo().currentBranch.empty());
    EXPECT_TRUE(ctx.gitInfo().commitHash.empty());
}

// ---------------------------------------------------------------------------
// loadFromDb returns null for missing session
// ---------------------------------------------------------------------------

TEST(SessionContextTest, LoadFromDb_NullForInvalidId) {
    auto ctx = SessionContext::loadFromDb(0, "/tmp/.a0", nullptr);
    EXPECT_FALSE(ctx);
}

// ---------------------------------------------------------------------------
// restore without persistence
// ---------------------------------------------------------------------------

TEST(SessionContextTest, Restore_NoWorktreePath_Fails) {
    // Create a context without a worktree path (as if init wasn't called)
    SessionContext ctx("/tmp", "/tmp/.a0", "abc123456789012345678901234567890", 0);
    // restore should fail since no worktree path was set
    int rc = ctx.restore(nullptr);
    EXPECT_NE(rc, 0);
}
