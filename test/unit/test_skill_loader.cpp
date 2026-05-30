#include "skills/skill_loader.h"
#include "skills/skills.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace a0::skills;

class SkillLoaderTest : public ::testing::Test {
protected:
    std::string m_root;
    SkillLoader* m_loader = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid());
        m_root = "/tmp/a0_sl_test_" + pid;
        fs::remove_all(m_root);
        fs::create_directories(m_root);
        m_loader = new SkillLoader(m_root);
    }

    void TearDown() override {
        delete m_loader;
        fs::remove_all(m_root);
    }

    void addComponent(const std::string& nsDir, const std::string& name, const SkillManifest& m) {
        std::string dir = m_root + "/" + nsDir + "/" + name;
        fs::create_directories(dir);
        json j;
        j["name"] = m.name;
        j["version"] = m.version;
        j["description"] = m.description;
        std::ofstream f(dir + "/skill.json");
        f << j.dump(2) << "\n";
    }
};

TEST_F(SkillLoaderTest, LoadAll_empty) {
    // Returns 0 because root dir exists (empty but loadable)
    EXPECT_EQ(m_loader->loadAll(), 0);
}

TEST_F(SkillLoaderTest, LoadAll_withSystemAndLocal) {
    fs::create_directories(m_root + "/system/base");
    fs::create_directories(m_root + "/local/mycomp");
    {
        std::ofstream f(m_root + "/system/base/skill.json");
        f << "{\"name\":\"base\",\"version\":\"1.0\"}\n";
    }
    {
        std::ofstream f(m_root + "/local/mycomp/skill.json");
        f << "{\"name\":\"mycomp\",\"version\":\"2.0\"}\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);

    SkillTool tool;
    Prompt prompt;
    // System tools are accessible
    EXPECT_EQ(m_loader->getTool("system", "base", "", tool), -2); // no tool named ""
    // Local components accessible
    EXPECT_EQ(m_loader->getPrompt("local", "mycomp", "", prompt), -2); // no prompt named ""
}

TEST_F(SkillLoaderTest, InvalidJsonFile) {
    fs::create_directories(m_root + "/local/bad");
    {
        std::ofstream f(m_root + "/local/bad/skill.json");
        f << "{invalid json,}\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    // Component should NOT be loaded
    SkillManifest m;
    EXPECT_EQ(m_loader->getManifest(SkillNamespace::LOCAL, "bad", m), -1);
}

TEST_F(SkillLoaderTest, MissingPromptFile) {
    fs::create_directories(m_root + "/local/test");
    {
        std::ofstream f(m_root + "/local/test/skill.json");
        f << "{\"name\":\"test\",\"prompts\":[{\"name\":\"p1\",\"promptFile\":\"does_not_exist.md\"}]}\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    Prompt p;
    EXPECT_EQ(m_loader->getPrompt("local", "test", "p1", p), 0);
    // Prompt should be loaded but content empty since file missing
    EXPECT_TRUE(p.prompt.empty());
}

TEST_F(SkillLoaderTest, addToolToExistingComponent) {
    fs::create_directories(m_root + "/local/mycomp");
    {
        std::ofstream f(m_root + "/local/mycomp/skill.json");
        f << "{\"name\":\"mycomp\",\"version\":\"1.0\"}\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);

    SkillTool tool1;
    tool1.name = "tool1";
    tool1.command = "echo 1";
    EXPECT_EQ(m_loader->addTool("mycomp", tool1), 0);

    SkillTool tool2;
    tool2.name = "tool2";
    tool2.command = "echo 2";
    EXPECT_EQ(m_loader->addTool("mycomp", tool2), 0);

    SkillTool got;
    EXPECT_EQ(m_loader->getTool("local", "mycomp", "tool1", got), 0);
    EXPECT_EQ(got.command, "echo 1");
}

TEST_F(SkillLoaderTest, updateTool_nonExistent) {
    fs::create_directories(m_root + "/local/mycomp");
    {
        std::ofstream f(m_root + "/local/mycomp/skill.json");
        f << "{\"name\":\"mycomp\",\"version\":\"1.0\"}\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);

    SkillTool tool;
    tool.name = "nonexistent";
    EXPECT_EQ(m_loader->updateTool("mycomp", "nonexistent", tool), -1);
}

TEST_F(SkillLoaderTest, removeComponent_readOnly) {
    fs::create_directories(m_root + "/system/syscomp");
    {
        std::ofstream f(m_root + "/system/syscomp/skill.json");
        f << "{\"name\":\"syscomp\",\"version\":\"1.0\"}\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    EXPECT_EQ(m_loader->removeComponent("syscomp"), -1);
}

TEST_F(SkillLoaderTest, removeComponent_nonExistent) {
    EXPECT_EQ(m_loader->removeComponent("ghost"), -1);
}

TEST_F(SkillLoaderTest, githubNamespace) {
    fs::create_directories(m_root + "/github_user/mycomp");
    {
        std::ofstream f(m_root + "/github_user/mycomp/skill.json");
        f << "{\"name\":\"mycomp\",\"version\":\"1.0\"}\n";
    }
    // loadAll creates system/ and local/ dirs, plus github_* dirs
    EXPECT_EQ(m_loader->loadAll(), 0);

    // Add tool via LOCAL namespace (writeManifest needs local/ dir to exist)
    fs::create_directories(m_root + "/local");
    SkillTool tool;
    tool.name = "mytool";
    tool.command = "echo hi";
    EXPECT_EQ(m_loader->addTool("mycomp", tool), 0);

    SkillTool got;
    EXPECT_EQ(m_loader->getTool("local", "mycomp", "mytool", got), 0);
}

TEST_F(SkillLoaderTest, writeSystemManifest) {
    SkillManifest m;
    m.name = "syscomp";
    m.version = "1.0";
    m.ns = SkillNamespace::SYSTEM;
    // SYSTEM namespace is read-only, so writeManifest returns -1
    EXPECT_NE(m_loader->writeManifest("syscomp", m), 0);
}
