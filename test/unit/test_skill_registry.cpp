#include "skill_registry.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

class SkillRegistryTest : public ::testing::Test {
protected:
    std::string m_testDir = "test_skills";
    FileSystemSkillRegistry registry;

    void SetUp() override {
        std::string cmd = "rm -rf " + m_testDir + " && mkdir -p " + m_testDir + "/system";
        system(cmd.c_str());
    }

    void TearDown() override {
        std::string cmd = "rm -rf " + m_testDir;
        system(cmd.c_str());
    }

    // Write a tool file under system/<name>/<name>.tool.json
    void writeTool(const std::string& name, const std::string& command) {
        std::string dir = m_testDir + "/system/" + name;
        std::string mkdir = "mkdir -p " + dir;
        system(mkdir.c_str());
        std::ofstream f(dir + "/" + name + ".tool.json");
        f << "{\"name\":\"" << name << "\",\"description\":\"test tool\","
          << "\"command\":\"" << command << "\",\"inputMode\":\"stdin\"}";
    }

    // Write a prompt file under system/<name>/<name>.prompt.json
    void writePrompt(const std::string& name, const std::string& promptTemplate,
                     const std::vector<std::string>& deps = {}) {
        std::string dir = m_testDir + "/system/" + name;
        std::string mkdir = "mkdir -p " + dir;
        system(mkdir.c_str());
        std::ofstream f(dir + "/" + name + ".prompt.json");
        f << "{\"name\":\"" << name << "\",\"description\":\"test prompt\","
          << "\"prompt\":\"" << promptTemplate << "\",\"dependencies\":[";
        for (size_t i = 0; i < deps.size(); ++i) {
            if (i) f << ",";
            f << "\"" << deps[i] << "\"";
        }
        f << "],\"validators\":[]}";
    }
};

TEST_F(SkillRegistryTest, LoadEmptyDirectory) {
    EXPECT_TRUE(registry.loadFromDirectory(m_testDir));
    EXPECT_TRUE(registry.listTools().empty());
    EXPECT_TRUE(registry.listPrompts().empty());
}

TEST_F(SkillRegistryTest, LoadTwoTools) {
    writeTool("tool_a", "echo a");
    writeTool("tool_b", "echo b");
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto tools = registry.listTools();
    EXPECT_EQ(tools.size(), 2u);
}

TEST_F(SkillRegistryTest, GetToolByName) {
    writeTool("my_tool", "cat");
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto tool = registry.getTool("my_tool");
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->name, "my_tool");
    EXPECT_EQ(tool->command, "cat");
}

TEST_F(SkillRegistryTest, GetToolNotFound) {
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    EXPECT_FALSE(registry.getTool("nonexistent").has_value());
}

TEST_F(SkillRegistryTest, LoadPromptsWithDeps) {
    writeTool("base_tool", "echo");
    writePrompt("my_prompt", "Do something", {"base_tool"});
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto pr = registry.getPrompt("my_prompt");
    ASSERT_TRUE(pr.has_value());
    ASSERT_EQ(pr->dependencies.size(), 1u);
    EXPECT_EQ(pr->dependencies[0], "base_tool");
}

TEST_F(SkillRegistryTest, MalformedJsonIsSkipped) {
    std::string badDir = m_testDir + "/system/badpkg";
    mkdir(badDir.c_str(), 0755);
    {
        std::ofstream f(badDir + "/bad.tool.json");
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

TEST_F(SkillRegistryTest, AddToolDynamically) {
    Tool t{"dyn_tool", "dynamic", "echo", "stdin"};
    EXPECT_TRUE(registry.addTool(t));
    auto tool = registry.getTool("dyn_tool");
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->command, "echo");
}

TEST_F(SkillRegistryTest, AddPromptDynamically) {
    Prompt p{"dyn_prompt", "desc", "prompt {{tool:bash}}", {"bash"}, {}};
    EXPECT_TRUE(registry.addPrompt(p));
    auto pr = registry.getPrompt("dyn_prompt");
    ASSERT_TRUE(pr.has_value());
    EXPECT_EQ(pr->prompt, "prompt {{tool:bash}}");
}

TEST_F(SkillRegistryTest, SkipFilesNotMatchingPattern) {
    {
        std::string badDir = m_testDir + "/system/other";
        mkdir(badDir.c_str(), 0755);
        std::ofstream f(badDir + "/readme.txt");
        f << "hello";
    }
    writeTool("only_one", "echo");
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    EXPECT_EQ(registry.listTools().size(), 1u);
}

TEST_F(SkillRegistryTest, LoadPromptWithValidators) {
    std::string pkgDir = m_testDir + "/system/val_prompt";
    mkdir(pkgDir.c_str(), 0755);
    {
        std::ofstream f(pkgDir + "/val_prompt.prompt.json");
        f << R"json({"name":"val_prompt","description":"with validators",)json"
          << R"json("prompt":"do stuff","dependencies":["bash"],"validators":[{"toolName":"extract_json"}]})json";
    }
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto prompts = registry.listPrompts();
    EXPECT_EQ(prompts.size(), 1u);
    if (prompts.size() >= 1) {
        auto pr = registry.getPrompt("val_prompt");
        ASSERT_TRUE(pr.has_value());
        ASSERT_EQ(pr->validators.size(), 1u);
        EXPECT_EQ(pr->validators[0].toolName, "extract_json");
    }
}

TEST_F(SkillRegistryTest, ListPromptsAfterLoad) {
    writePrompt("p1", "prompt1", {});
    writePrompt("p2", "prompt2", {});
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    auto prompts = registry.listPrompts();
    EXPECT_EQ(prompts.size(), 2u);
}

TEST_F(SkillRegistryTest, LoadNonexistentDirectory) {
    EXPECT_FALSE(registry.loadFromDirectory("/nonexistent_path_xyz"));
}

TEST_F(SkillRegistryTest, AddToolPersistsToDisk) {
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    Tool t{"disk_tool", "disk", "echo", "stdin"};
    EXPECT_TRUE(registry.addTool(t));
    // reload from disk to verify persistence
    FileSystemSkillRegistry reg2;
    ASSERT_TRUE(reg2.loadFromDirectory(m_testDir));
    auto tool = reg2.getTool("disk_tool");
    ASSERT_TRUE(tool.has_value());
    EXPECT_EQ(tool->command, "echo");
}

TEST_F(SkillRegistryTest, AddPromptPersistsToDisk) {
    ASSERT_TRUE(registry.loadFromDirectory(m_testDir));
    Prompt p{"disk_prompt", "desc", "prompt {{tool:bash}}", {"bash"}, {}};
    EXPECT_TRUE(registry.addPrompt(p));
    FileSystemSkillRegistry reg2;
    ASSERT_TRUE(reg2.loadFromDirectory(m_testDir));
    auto pr = reg2.getPrompt("disk_prompt");
    ASSERT_TRUE(pr.has_value());
    EXPECT_EQ(pr->prompt, "prompt {{tool:bash}}");
}
