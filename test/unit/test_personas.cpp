#include "bootstrap/personas.h"
#include "bootstrap/base_prompt.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace a0::personas;

class PersonaLoaderTest : public ::testing::Test {
protected:
    std::string m_root;

    void SetUp() override {
        m_root = "/tmp/a0_persona_test_" + std::to_string(::getpid());
        fs::remove_all(m_root);
    }

    void TearDown() override {
        fs::remove_all(m_root);
    }

    void writePersona(const std::string& ns, const std::string& name,
                       const std::string& promptContent = "") {
        fs::create_directories(m_root + "/" + ns + "/" + name);
        json j;
        j["name"] = name;
        j["description"] = "test " + name;
        j["promptFile"] = "prompt.md";
        if (name == "sw") {
            j["skills"] = json::array({"system_task_manager"});
            j["tools"] = json::array({"system_fs_read"});
        }
        {
            std::ofstream f(m_root + "/" + ns + "/" + name + "/persona.json");
            f << j.dump(2) << "\n";
        }
        {
            std::ofstream f(m_root + "/" + ns + "/" + name + "/prompt.md");
            f << (promptContent.empty() ? "You are " + name + "." : promptContent);
        }
    }

    void writePersonaNoPromptFile(const std::string& ns, const std::string& name) {
        fs::create_directories(m_root + "/" + ns + "/" + name);
        json j;
        j["name"] = name;
        j["description"] = "test " + name;
        j["promptFile"] = "nonexistent.md";
        {
            std::ofstream f(m_root + "/" + ns + "/" + name + "/persona.json");
            f << j.dump(2) << "\n";
        }
    }

    void writePersonaInvalidJson(const std::string& ns, const std::string& name) {
        fs::create_directories(m_root + "/" + ns + "/" + name);
        {
            std::ofstream f(m_root + "/" + ns + "/" + name + "/persona.json");
            f << "{invalid json,}\n";
        }
    }

    void writePersonaMissingFields(const std::string& ns, const std::string& name) {
        fs::create_directories(m_root + "/" + ns + "/" + name);
        json j;
        j["name"] = name;
        {
            std::ofstream f(m_root + "/" + ns + "/" + name + "/persona.json");
            f << j.dump(2) << "\n";
        }
    }
};

TEST_F(PersonaLoaderTest, LoadAll_ValidPersonas) {
    writePersona("system", "se");
    writePersona("system", "pd");
    PersonaLoader loader(m_root);
    EXPECT_EQ(loader.loadAll(), 0);
    auto list = loader.listPersonas();
    EXPECT_EQ(list.size(), 2u);
}

TEST_F(PersonaLoaderTest, LoadAll_MissingRoot) {
    PersonaLoader loader(m_root + "/nonexistent");
    EXPECT_EQ(loader.loadAll(), -1);
}

TEST_F(PersonaLoaderTest, LoadAll_EmptyRoot) {
    fs::create_directories(m_root + "/system");
    PersonaLoader loader(m_root);
    EXPECT_EQ(loader.loadAll(), 0);
    EXPECT_TRUE(loader.listPersonas().empty());
}

TEST_F(PersonaLoaderTest, LoadAll_MissingPromptFile) {
    writePersonaNoPromptFile("system", "noprompt");
    PersonaLoader loader(m_root);
    EXPECT_EQ(loader.loadAll(), 0);
    auto persona = loader.getPersona("noprompt");
    ASSERT_TRUE(persona.has_value());
    EXPECT_TRUE(persona->prompt.empty());
}

TEST_F(PersonaLoaderTest, LoadAll_SkipsInvalidJson) {
    writePersona("system", "good", "hello");
    writePersonaInvalidJson("system", "bad");
    PersonaLoader loader(m_root);
    EXPECT_EQ(loader.loadAll(), 0);
    auto list = loader.listPersonas();
    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].manifest.name, "good");
}

TEST_F(PersonaLoaderTest, LoadAll_SkipsMissingFields) {
    writePersonaMissingFields("system", "bad");
    writePersona("system", "good", "hello");
    PersonaLoader loader(m_root);
    EXPECT_EQ(loader.loadAll(), 0);
    auto list = loader.listPersonas();
    EXPECT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].manifest.name, "good");
}

TEST_F(PersonaLoaderTest, LoadAll_AllNamespaces) {
    writePersona("system", "sys");
    writePersona("local", "loc");
    writePersona("github_testuser", "gh");
    PersonaLoader loader(m_root);
    EXPECT_EQ(loader.loadAll(), 0);
    EXPECT_EQ(loader.listPersonas().size(), 3u);
}

TEST_F(PersonaLoaderTest, GetPersona_ByName) {
    writePersona("system", "my-persona", "You are my-persona.");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    auto p = loader.getPersona("my-persona");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->manifest.name, "my-persona");
    EXPECT_EQ(p->manifest.description, "test my-persona");
    EXPECT_EQ(p->prompt, "You are my-persona.");
}

TEST_F(PersonaLoaderTest, GetPersona_CaseInsensitive) {
    writePersona("system", "CamelCase", "prompt");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    EXPECT_TRUE(loader.getPersona("camelcase").has_value());
    EXPECT_TRUE(loader.getPersona("CAMELCASE").has_value());
    EXPECT_TRUE(loader.getPersona("CamelCase").has_value());
}

TEST_F(PersonaLoaderTest, GetPersona_NotFound) {
    writePersona("system", "exists", "prompt");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    EXPECT_FALSE(loader.getPersona("nonexistent").has_value());
}

TEST_F(PersonaLoaderTest, GetPersona_SkillsAndTools) {
    writePersona("system", "sw", "prompt");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    auto p = loader.getPersona("sw");
    ASSERT_TRUE(p.has_value());
    ASSERT_EQ(p->manifest.skills.size(), 1u);
    EXPECT_EQ(p->manifest.skills[0], "system_task_manager");
    ASSERT_EQ(p->manifest.tools.size(), 1u);
    EXPECT_EQ(p->manifest.tools[0], "system_fs_read");
}

TEST_F(PersonaLoaderTest, ListPersonas_Count) {
    writePersona("system", "a");
    writePersona("system", "b");
    writePersona("local", "c");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    EXPECT_EQ(loader.listPersonas().size(), 3u);
}

TEST_F(PersonaLoaderTest, LoadAll_Idempotent) {
    writePersona("system", "p1", "prompt1");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    EXPECT_EQ(loader.listPersonas().size(), 1u);
    ASSERT_EQ(loader.loadAll(), 0);
    EXPECT_EQ(loader.listPersonas().size(), 1u);
    ASSERT_EQ(loader.loadAll(), 0);
    EXPECT_EQ(loader.listPersonas().size(), 1u);
}

TEST_F(PersonaLoaderTest, GithubNamespaceWithUnderscore) {
    writePersona("github_some_user", "my-tool", "tool prompt");
    PersonaLoader loader(m_root);
    ASSERT_EQ(loader.loadAll(), 0);
    auto p = loader.getPersona("my-tool");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->manifest.name, "my-tool");
}
