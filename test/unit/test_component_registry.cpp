#include "component_registry.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

class ComponentRegistryTest : public ::testing::Test {
protected:
    std::string m_testDir = "test_components";
    FileSystemComponentRegistry registry;

    void SetUp() override {
        std::string cmd = "rm -rf " + m_testDir + " && mkdir -p " + m_testDir;
        system(cmd.c_str());
    }

    void TearDown() override {
        std::string cmd = "rm -rf " + m_testDir;
        system(cmd.c_str());
    }

    void writeTool(const std::string& name, const std::string& command) {
        std::ofstream f(m_testDir + "/" + name + ".tool.json");
        f << "{\"name\":\"" << name << "\",\"description\":\"test tool\","
          << "\"command\":\"" << command << "\",\"inputMode\":\"stdin\"}";
    }

    void writeSkill(const std::string& name, const std::string& prompt,
                    const std::vector<std::string>& deps = {}) {
        std::ofstream f(m_testDir + "/" + name + ".skill.json");
        f << "{\"name\":\"" << name << "\",\"description\":\"test skill\","
          << "\"prompt\":\"" << prompt << "\",\"dependencies\":[";
        for (size_t i = 0; i < deps.size(); ++i) {
            if (i) f << ",";
            f << "\"" << deps[i] << "\"";
        }
        f << "],\"validators\":[]}";
    }
};

TEST_F(ComponentRegistryTest, LoadEmptyDirectory) {
    EXPECT_TRUE(registry.loadFromDirectory(m_testDir));
    EXPECT_TRUE(registry.listTools().empty());
    EXPECT_TRUE(registry.listSkills().empty());
}

TEST_F(ComponentRegistryTest, LoadTwoTools) {
    writeTool("tool_a", "echo a");
    writeTool("tool_b", "echo b");
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto tools = registry.listTools();
    EXPECT_EQ(tools.size(), 2u);
}

TEST_F(ComponentRegistryTest, GetToolByName) {
    writeTool("my_tool", "cat");
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto tool = registry.getTool("my_tool");
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->name, "my_tool");
    EXPECT_EQ(tool->command, "cat");
}

TEST_F(ComponentRegistryTest, GetToolNotFound) {
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    EXPECT_FALSE(registry.getTool("nonexistent").has_value());
}

TEST_F(ComponentRegistryTest, LoadSkillsWithDeps) {
    writeTool("base_tool", "echo");
    writeSkill("my_skill", "Do something", {"base_tool"});
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto skill = registry.getSkill("my_skill");
    ASSERT_TRUE(skill.has_value());
    ASSERT_EQ(skill->dependencies.size(), 1u);
    EXPECT_EQ(skill->dependencies[0], "base_tool");
}

TEST_F(ComponentRegistryTest, MalformedJsonIsSkipped) {
    {
        std::ofstream f(m_testDir + "/bad.tool.json");
        f << "not valid json";
    }
    writeTool("good", "echo");
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto tools = registry.listTools();
    EXPECT_EQ(tools.size(), 1u);
    if (tools.size() > 0) {
        EXPECT_EQ(tools[0], "good");
    }
}

TEST_F(ComponentRegistryTest, AddToolDynamically) {
    Tool t{"dyn_tool", "dynamic", "echo", "stdin"};
    EXPECT_TRUE(registry.addTool(t));
    auto tool = registry.getTool("dyn_tool");
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->command, "echo");
}

TEST_F(ComponentRegistryTest, AddSkillDynamically) {
    Skill s{"dyn_skill", "desc", "prompt {{tool:bash}}", {"bash"}, {}};
    EXPECT_TRUE(registry.addSkill(s));
    auto skill = registry.getSkill("dyn_skill");
    ASSERT_TRUE(skill.has_value());
    EXPECT_EQ(skill->prompt, "prompt {{tool:bash}}");
}

TEST_F(ComponentRegistryTest, SkipFilesNotMatchingPattern) {
    {
        std::ofstream f(m_testDir + "/readme.txt");
        f << "hello";
    }
    writeTool("only_one", "echo");
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    EXPECT_EQ(registry.listTools().size(), 1u);
}

TEST_F(ComponentRegistryTest, LoadSkillWithValidators) {
    {
        std::ofstream f(m_testDir + "/val_skill.skill.json");
        f << R"json({"name":"val_skill","description":"with validators",)json"
          << R"json("prompt":"do stuff","dependencies":["bash"],"validators":[{"toolName":"extract_json"}]})json";
    }
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto skills = registry.listSkills();
    EXPECT_EQ(skills.size(), 1u);
    if (skills.size() >= 1) {
        auto skill = registry.getSkill("val_skill");
        ASSERT_TRUE(skill.has_value());
        ASSERT_EQ(skill->validators.size(), 1u);
        EXPECT_EQ(skill->validators[0].toolName, "extract_json");
    }
}

TEST_F(ComponentRegistryTest, ListSkillsAfterLoad) {
    writeSkill("s1", "prompt1", {});
    writeSkill("s2", "prompt2", {});
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto skills = registry.listSkills();
    EXPECT_EQ(skills.size(), 2u);
}

TEST_F(ComponentRegistryTest, LoadNonexistentDirectory) {
    EXPECT_FALSE(registry.loadFromDirectory("/nonexistent_path_xyz"));
}

TEST_F(ComponentRegistryTest, AddToolPersistsToDisk) {
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    Tool t{"disk_tool", "disk", "echo", "stdin"};
    EXPECT_TRUE(registry.addTool(t));
    // reload from disk to verify persistence
    FileSystemComponentRegistry reg2;
    ASSERT_TRUE(reg2.loadFromDirectory(m_testDir));
    auto tool = reg2.getTool("disk_tool");
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->command, "echo");
}

TEST_F(ComponentRegistryTest, AddSkillPersistsToDisk) {
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    Skill s{"disk_skill", "desc", "prompt {{tool:bash}}", {"bash"}, {}};
    EXPECT_TRUE(registry.addSkill(s));
    FileSystemComponentRegistry reg2;
    ASSERT_TRUE(reg2.loadFromDirectory(m_testDir));
    auto skill = reg2.getSkill("disk_skill");
    ASSERT_TRUE(skill.has_value());
    EXPECT_EQ(skill->prompt, "prompt {{tool:bash}}");
}
