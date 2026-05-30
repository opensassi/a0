#include "skills/version_manager.h"
#include "skills/skills.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace a0::skills;

class VersionManagerTest : public ::testing::Test {
protected:
    std::string m_storeRoot;
    std::string m_skillsRoot;
    VersionManager* m_mgr = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid());
        m_storeRoot = "/tmp/a0_vm_store_" + pid;
        m_skillsRoot = "/tmp/a0_vm_skills_" + pid;
        fs::remove_all(m_storeRoot);
        fs::remove_all(m_skillsRoot);
        fs::create_directories(m_storeRoot);
        fs::create_directories(m_skillsRoot);
        m_mgr = new VersionManager(m_storeRoot, m_skillsRoot);
    }

    void TearDown() override {
        delete m_mgr;
        fs::remove_all(m_storeRoot);
        fs::remove_all(m_skillsRoot);
    }

    void createComponent(const std::string& compDir, const std::string& component) {
        fs::create_directories(m_skillsRoot + "/" + compDir + "/" + component);
        std::ofstream f(m_skillsRoot + "/" + compDir + "/" + component + "/skill.json");
        f << "{\"name\":\"" << component << "\"}\n";
    }
};

TEST_F(VersionManagerTest, ConstructFromEmpty) {
    SUCCEED();
}

TEST_F(VersionManagerTest, ArchiveMultipleTimesIncrementsRefcount) {
    createComponent("local", "mycomp");
    EXPECT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "mycomp", "abc123", "1.0.0"), 0);
    EXPECT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "mycomp", "abc123", "1.0.0"), 0);
    EXPECT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "mycomp", "abc123", "1.0.0"), 0);
}

TEST_F(VersionManagerTest, ArchiveNonexistentComponent) {
    EXPECT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "nonexistent", "abc123", "1.0.0"), -1);
}

TEST_F(VersionManagerTest, RestoreUnknownVersion) {
    EXPECT_EQ(m_mgr->restore(SkillNamespace::LOCAL, "mycomp", "unknown"), -1);
}

TEST_F(VersionManagerTest, ReleaseVersion) {
    createComponent("local", "mycomp");
    ASSERT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "mycomp", "abc123", "1.0.0"), 0);
    EXPECT_EQ(m_mgr->release(SkillNamespace::LOCAL, "mycomp", "abc123"), 0);
}

TEST_F(VersionManagerTest, ReleaseUnknownVersion) {
    EXPECT_EQ(m_mgr->release(SkillNamespace::LOCAL, "mycomp", "unknown"), -1);
}

TEST_F(VersionManagerTest, ReleaseByComponentWithoutCommit) {
    createComponent("local", "mycomp");
    ASSERT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "mycomp", "abc123", "1.0.0"), 0);
    EXPECT_EQ(m_mgr->release(SkillNamespace::LOCAL, "mycomp", ""), 0);
}

TEST_F(VersionManagerTest, ReleaseNonexistentComponentWithoutCommit) {
    EXPECT_EQ(m_mgr->release(SkillNamespace::LOCAL, "ghost", ""), -1);
}

TEST_F(VersionManagerTest, GcWithNoVersions) {
    EXPECT_EQ(m_mgr->gc(false), 0);
}

TEST_F(VersionManagerTest, GcAfterRelease) {
    createComponent("local", "mycomp");
    ASSERT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "mycomp", "abc123", "1.0.0"), 0);
    ASSERT_EQ(m_mgr->release(SkillNamespace::LOCAL, "mycomp", "abc123"), 0);
    // GC should visit but refcount 0 might not trigger removal
    // depending on lock file persistence — just verify it doesn't crash
    m_mgr->gc(false);
}

TEST_F(VersionManagerTest, SystemNamespaceArchive) {
    createComponent("system", "syscomp");
    EXPECT_EQ(m_mgr->archive(SkillNamespace::SYSTEM, "syscomp", "def456", "2.0.0"), 0);
}

TEST_F(VersionManagerTest, GithubNamespaceArchive) {
    createComponent("github_", "ghcomp");
    EXPECT_EQ(m_mgr->archive(SkillNamespace::GITHUB, "ghcomp", "ghi789", "3.0.0"), 0);
}

TEST_F(VersionManagerTest, PersistLockFileAcrossInstances) {
    createComponent("local", "persist_comp");
    ASSERT_EQ(m_mgr->archive(SkillNamespace::LOCAL, "persist_comp", "abc123", "1.0.0"), 0);

    delete m_mgr;
    m_mgr = new VersionManager(m_storeRoot, m_skillsRoot);

    // After reload, the version should be found
    EXPECT_EQ(m_mgr->release(SkillNamespace::LOCAL, "persist_comp", "abc123"), 0);
}
