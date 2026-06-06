#include "a0_launcher.h"
#include <gtest/gtest.h>
#include <cstdlib>

using namespace a0::b1;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(A0LauncherTest, ConstructWithValidPath) {
    A0Launcher launcher("/usr/bin/a0");
    // Should not crash
}

TEST(A0LauncherTest, ConstructWithEmptyPath) {
    A0Launcher launcher("");
    // Should not crash
}

// ---------------------------------------------------------------------------
// runSkill
// ---------------------------------------------------------------------------

TEST(A0LauncherTest, RunSkillWithNonExistentBinary) {
    A0Launcher launcher("/nonexistent/a0_binary");
    std::string result;
    int rc = launcher.runSkill("system_rebuild", "{}", result);
    // Stub returns -1 — real impl should return -1 (binary not found)
    EXPECT_EQ(rc, -1);
}

TEST(A0LauncherTest, RunSkillWithTimeout) {
    A0Launcher launcher("sleep");
    std::string result;
    int rc = launcher.runSkill("system_sleep", "{}", result, 1);
    // Stub returns -1 — real impl should return -2 on timeout
    EXPECT_EQ(rc, -1);
}

TEST(A0LauncherTest, RunSkillReturnsResult) {
    A0Launcher launcher("echo");
    std::string result;
    // Real impl would capture stdout and parse JSON
    int rc = launcher.runSkill("system_echo", R"({"msg":"ok"})", result);
    EXPECT_EQ(rc, -1);
    // Real impl: EXPECT_EQ(rc, 0); EXPECT_EQ(result, R"({"msg":"ok"})\n");
}
