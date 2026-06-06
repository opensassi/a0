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

TEST_F(SkillLoaderTest, LoadWithSubModules) {
    fs::create_directories(m_root + "/local/maincomp/submod1");
    fs::create_directories(m_root + "/local/maincomp/submod2");
    {
        std::ofstream f(m_root + "/local/maincomp/skill.json");
        f << R"({"name":"maincomp","version":"1.0","subModules":["submod1","submod2"]})" << "\n";
    }
    {
        std::ofstream f(m_root + "/local/maincomp/submod1/skill.json");
        f << R"({"name":"submod1","version":"1.0","tools":[{"name":"t1","command":"echo t1"}]})" << "\n";
    }
    {
        std::ofstream f(m_root + "/local/maincomp/submod2/skill.json");
        f << R"({"name":"submod2","version":"1.0","tools":[{"name":"t2","command":"echo t2"}]})" << "\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    SkillTool tool;
    // Sub-modules get indexed under "maincomp-submod1" component key
    EXPECT_EQ(m_loader->getTool("local", "maincomp-submod1", "t1", tool), 0);
    EXPECT_EQ(tool.command, "echo t1");
}

TEST_F(SkillLoaderTest, GithubNamespaceScanned) {
    fs::create_directories(m_root + "/github_testuser/mycomp");
    {
        std::ofstream f(m_root + "/github_testuser/mycomp/skill.json");
        f << R"({"name":"mycomp","version":"1.0","tools":[{"name":"gtool","command":"echo gh"}]})" << "\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    SkillTool tool;
    EXPECT_EQ(m_loader->getTool("github_testuser", "mycomp", "gtool", tool), 0);
    EXPECT_EQ(tool.command, "echo gh");
}

TEST_F(SkillLoaderTest, GetToolNotFound) {
    fs::create_directories(m_root + "/local/comp");
    {
        std::ofstream f(m_root + "/local/comp/skill.json");
        f << R"({"name":"comp","version":"1.0","tools":[{"name":"t1","command":"echo"}]})" << "\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    SkillTool tool;
    EXPECT_EQ(m_loader->getTool("local", "nonexistent", "t1", tool), -1);
    EXPECT_EQ(m_loader->getTool("local", "comp", "nonexistent_tool", tool), -2);
}

TEST_F(SkillLoaderTest, GetPromptNotFound) {
    fs::create_directories(m_root + "/local/comp");
    {
        std::ofstream f(m_root + "/local/comp/skill.json");
        f << R"({"name":"comp","version":"1.0","prompts":[{"name":"p1","prompt":"hello"}]})" << "\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    Prompt prompt;
    EXPECT_EQ(m_loader->getPrompt("local", "nonexistent", "p1", prompt), -1);
    EXPECT_EQ(m_loader->getPrompt("local", "comp", "nonexistent_prompt", prompt), -2);
}

TEST_F(SkillLoaderTest, RemoveComponentLocal) {
    fs::create_directories(m_root + "/local/removable");
    {
        std::ofstream f(m_root + "/local/removable/skill.json");
        f << R"({"name":"removable","version":"1.0"})" << "\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    EXPECT_EQ(m_loader->removeComponent("removable"), 0);
    EXPECT_FALSE(fs::exists(m_root + "/local/removable"));
}

TEST_F(SkillLoaderTest, WriteManifestWritesToDisk) {
    fs::create_directories(m_root + "/local");
    SkillManifest m;
    m.name = "testwrite";
    m.version = "2.0.0";
    m.ns = SkillNamespace::LOCAL;
    SkillTool tool;
    tool.name = "tw";
    tool.command = "echo test";
    m.tools.push_back(tool);
    EXPECT_EQ(m_loader->writeManifest("testwrite", m), 0);
    EXPECT_TRUE(fs::exists(m_root + "/local/testwrite/skill.json"));
}

TEST_F(SkillLoaderTest, LoadManifestWithToolDetails) {
    fs::create_directories(m_root + "/local/detail");
    {
        std::ofstream f(m_root + "/local/detail/skill.json");
        f << R"({"name":"detail","version":"1.0","tools":[{"name":"t","command":"echo","parameters":{"type":"object"}}]})" << "\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    SkillTool tool;
    EXPECT_EQ(m_loader->getTool("local", "detail", "t", tool), 0);
    EXPECT_EQ(tool.name, "t");
    EXPECT_EQ(tool.command, "echo");
}

TEST_F(SkillLoaderTest, LoadManifestWithAllToolFields) {
    fs::create_directories(m_root + "/local/full");
    {
        std::ofstream f(m_root + "/local/full/skill.json");
        f << R"({
            "name":"full","version":"2.0",
            "tools":[{
                "name":"t","command":"echo","inputMode":"args",
                "description":"a tool","systemTool":true,"default":true,
                "timeoutSecs":60,"streaming":true,"subCommand":"sub",
                "trustLevel":"HIGH","dockerImage":"ubuntu:22.04",
                "aptDependencies":["curl"],
                "parameters":{"type":"object"}
            }],
            "prompts":[{
                "name":"p","prompt":"hello","description":"a prompt",
                "dependencies":["dep1"],"chain":["base"],
                "parallelValidators":true,
                "validators":[{"toolName":"v1"}]
            }]
        })" << "\n";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    SkillTool tool;
    ASSERT_EQ(m_loader->getTool("local", "full", "t", tool), 0);
    EXPECT_EQ(tool.name, "t");
    EXPECT_TRUE(tool.systemTool);
    EXPECT_TRUE(tool.default_);
    EXPECT_EQ(tool.timeoutSecs, 60);
    EXPECT_TRUE(tool.streaming);
    EXPECT_EQ(tool.trustLevel, TrustLevel::HIGH);
    ASSERT_EQ(tool.aptDependencies.size(), 1u);
    EXPECT_EQ(tool.aptDependencies[0], "curl");
}

TEST_F(SkillLoaderTest, LoadManifestWithSubModulesError) {
    fs::create_directories(m_root + "/local/parent/sub");
    {
        std::ofstream f(m_root + "/local/parent/skill.json");
        f << R"({"name":"parent","version":"1.0","subModules":["sub"]})" << "\n";
    }
    {
        std::ofstream f(m_root + "/local/parent/sub/skill.json");
        f << "invalid json";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    // Should not crash, just skip the sub-module
}

TEST_F(SkillLoaderTest, PromptFileFound) {
    fs::create_directories(m_root + "/local/promptdir");
    {
        std::ofstream f(m_root + "/local/promptdir/skill.json");
        f << R"({"name":"promptdir","version":"1.0","prompts":[{"name":"p","promptFile":"content.md"}]})" << "\n";
    }
    {
        std::ofstream f(m_root + "/local/promptdir/content.md");
        f << "file content";
    }
    EXPECT_EQ(m_loader->loadAll(), 0);
    Prompt p;
    ASSERT_EQ(m_loader->getPrompt("local", "promptdir", "p", p), 0);
    EXPECT_EQ(p.prompt, "file content");
}
