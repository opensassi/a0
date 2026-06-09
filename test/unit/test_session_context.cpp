#include "bootstrap/session_context.h"
#include "shared/hex_session_id.h"
#include "bootstrap/base_prompt.h"
#include "skills/skills.h"
#include <gtest/gtest.h>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;
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

// ---------------------------------------------------------------------------
// generateHexSessionId
// ---------------------------------------------------------------------------

TEST(SessionContextTest, HexSessionId_NonEmpty) {
    std::string id = generateHexSessionId();
    EXPECT_FALSE(id.empty());
}

TEST(SessionContextTest, HexSessionId_Length) {
    std::string id = generateHexSessionId();
    // 4 uint32_t values, each formatted as 8 hex chars = 32 chars
    EXPECT_EQ(id.size(), 32u);
}

TEST(SessionContextTest, HexSessionId_HexChars) {
    std::string id = generateHexSessionId();
    for (char c : id) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST(SessionContextTest, HexSessionId_Unique) {
    std::string id1 = generateHexSessionId();
    std::string id2 = generateHexSessionId();
    EXPECT_NE(id1, id2);
}

// ---------------------------------------------------------------------------
// buildBasePrompt
// ---------------------------------------------------------------------------

TEST(SessionContextTest, BuildBasePrompt_ReturnsString) {
    std::string prompt = buildBasePrompt(nullptr);
    EXPECT_FALSE(prompt.empty());
}

TEST(SessionContextTest, LoadFromDb_WithNullPersistence) {
    auto ctx = SessionContext::loadFromDb(42, "/tmp/.a0", nullptr);
    EXPECT_FALSE(ctx);
}

TEST(SessionContextTest, LoadFromDb_WithNegativeId) {
    auto ctx = SessionContext::loadFromDb(-1, "/tmp/.a0", nullptr);
    EXPECT_FALSE(ctx);
}

TEST(SessionContextTest, WorktreePath_EmptyByDefault) {
    SessionContext ctx("/tmp", "/tmp/.a0", "abc123456789012345678901234567890", 0);
    EXPECT_TRUE(ctx.worktreePath().empty());
}

TEST(SessionContextTest, Init_NullManagerReturnsError) {
    SessionContext ctx("/tmp", "/tmp/.a0", "abc123456789012345678901234567890", 0);
    int rc = ctx.init(nullptr);
    EXPECT_NE(rc, 0);
}
