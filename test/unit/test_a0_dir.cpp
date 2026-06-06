#include "a0_dir.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class A0DirTest : public ::testing::Test {
protected:
    std::string m_tmp;

    void SetUp() override {
        m_tmp = "/tmp/a0_dir_test_" + std::to_string(::getpid());
        fs::remove_all(m_tmp);
        fs::create_directories(m_tmp);
    }

    void TearDown() override {
        fs::remove_all(m_tmp);
    }

    // Create a .git directory so isGitRepository returns true
    void makeGitRepo() {
        fs::create_directories(m_tmp + "/.git");
    }

    // Create a .gitignore with given content
    void makeGitignore(const std::string& content) {
        std::ofstream f(m_tmp + "/.gitignore");
        f << content;
    }
};

TEST_F(A0DirTest, CreateNewDir) {
    std::string path = m_tmp + "/.a0";
    int rc = a0::ensureA0Dir(path);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::is_directory(path));
}

TEST_F(A0DirTest, DirAlreadyExists) {
    std::string path = m_tmp + "/.a0";
    fs::create_directories(path);
    int rc = a0::ensureA0Dir(path);
    EXPECT_EQ(rc, 1);
}

TEST_F(A0DirTest, EmptyPath) {
    int rc = a0::ensureA0Dir("");
    EXPECT_EQ(rc, -1);
}

TEST_F(A0DirTest, NewDirInGitRepo_addsGitignore) {
    fs::current_path(m_tmp);
    makeGitRepo();
    std::string path = m_tmp + "/.a0";
    int rc = a0::ensureA0Dir(path);
    EXPECT_EQ(rc, 0);

    std::ifstream f(m_tmp + "/.gitignore");
    ASSERT_TRUE(f.good());
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find(".a0/"), std::string::npos);
    fs::current_path("/");
}

TEST_F(A0DirTest, NewDirInGitRepo_alreadyIgnored) {
    fs::current_path(m_tmp);
    makeGitRepo();
    makeGitignore(".a0\n");
    std::string path = m_tmp + "/.a0";
    int rc = a0::ensureA0Dir(path);
    EXPECT_EQ(rc, 0);

    // Should not append duplicate
    std::ifstream f(m_tmp + "/.gitignore");
    ASSERT_TRUE(f.good());
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    size_t count = 0;
    for (size_t pos = 0; (pos = content.find(".a0", pos)) != std::string::npos; pos++) {
        count++;
    }
    EXPECT_EQ(count, 1u);
    fs::current_path("/");
}

TEST_F(A0DirTest, NestedDirCreation) {
    std::string path = m_tmp + "/a/b/c/.a0";
    int rc = a0::ensureA0Dir(path);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::is_directory(path));
}

TEST_F(A0DirTest, CreateDirOutsideGitRepo) {
    // No .git directory in m_tmp
    fs::current_path(m_tmp);
    std::string path = m_tmp + "/.a0";
    int rc = a0::ensureA0Dir(path);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::is_directory(path));
    // .gitignore should exist only if CWD was a git repo — since we set CWD to m_tmp (not a repo), it should not
    EXPECT_FALSE(fs::exists(m_tmp + "/.gitignore"));
    fs::current_path("/");
}

TEST_F(A0DirTest, RequireWorktreeMissing) {
    std::string path = m_tmp + "/.a0";
    int rc = a0::ensureA0Dir(path, true);
    EXPECT_EQ(rc, -1);
}

TEST_F(A0DirTest, RequireWorktreeExisting) {
    std::string path = m_tmp + "/.a0";
    fs::create_directories(path + "/worktrees");
    int rc = a0::ensureA0Dir(path, true);
    EXPECT_EQ(rc, 1);
}

TEST_F(A0DirTest, NewDirWorktreeCreated) {
    std::string path = m_tmp + "/.a0";
    int rc = a0::ensureA0Dir(path, false);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(fs::is_directory(path + "/worktrees"));
}
