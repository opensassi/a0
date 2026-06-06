#include "skills/skills.h"
#include "dependency_resolver.h"
#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;
using namespace a0::skills;

class DependencyResolverTest : public ::testing::Test {
protected:
    SkillManager* m_mgr = nullptr;
    DefaultDependencyResolver* m_resolver = nullptr;

    void SetUp() override {
        std::string pid = std::to_string(::getpid());
        m_mgr = new SkillManager(
            "/tmp/a0_test_dep_res_skills_" + pid,
            "/tmp/a0_test_dep_res_store_" + pid,
            nullptr
        );

        SkillTool tool;
        tool.name = "base_tool";
        tool.description = "base";
        tool.command = "echo";
        tool.inputMode = "stdin";
        m_mgr->addTool("test", tool);

        Prompt prompt;
        prompt.name = "base_skill";
        prompt.description = "desc";
        prompt.prompt = "do something";
        m_mgr->addPrompt("test", prompt);

        m_resolver = new DefaultDependencyResolver(m_mgr);
    }

    void TearDown() override {
        delete m_resolver;
        delete m_mgr;
        std::string pid = std::to_string(::getpid());
        fs::remove_all("/tmp/a0_test_dep_res_skills_" + pid);
        fs::remove_all("/tmp/a0_test_dep_res_store_" + pid);
        fs::remove_all("/tmp/a0_test_dep_res_logs_" + pid);
    }
};

TEST_F(DependencyResolverTest, ToolHasNoDependencies) {
    Tool t;
    t.name = "simple";
    EXPECT_TRUE(m_resolver->checkToolDependencies(t));
}

TEST_F(DependencyResolverTest, QualifiedToolDepFound) {
    Prompt p;
    p.name = "test_skill";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"local_test_base_tool", "local_test_base_skill"};
    EXPECT_TRUE(m_resolver->checkPromptDependencies(p));
}

TEST_F(DependencyResolverTest, UnqualifiedDepReportedMissing) {
    Prompt p;
    p.name = "broken";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"nonexistent"};
    EXPECT_FALSE(m_resolver->checkPromptDependencies(p));
}

TEST_F(DependencyResolverTest, MissingDepsList) {
    Prompt p;
    p.name = "check";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"local_test_base_tool", "missing_a", "missing_b"};
    auto missing = m_resolver->missingDependencies(p);
    EXPECT_EQ(missing.size(), 2u);
}

TEST_F(DependencyResolverTest, SkillWithNoDependencies) {
    Prompt p;
    p.name = "standalone";
    p.description = "desc";
    p.prompt = "prompt";
    EXPECT_TRUE(m_resolver->checkPromptDependencies(p));
    EXPECT_TRUE(m_resolver->missingDependencies(p).empty());
}

TEST_F(DependencyResolverTest, SelfReferencingDependency) {
    Prompt p;
    p.name = "self_ref";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"self_ref"};
    EXPECT_FALSE(m_resolver->checkPromptDependencies(p));
    auto missing = m_resolver->missingDependencies(p);
    EXPECT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], "self_ref");
}

TEST_F(DependencyResolverTest, CircularDependencyReported) {
    Prompt p;
    p.name = "circular";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"a", "b"};
    auto missing = m_resolver->missingDependencies(p);
    EXPECT_EQ(missing.size(), 2u);
}

TEST_F(DependencyResolverTest, ManagerWithNoComponents) {
    std::string pid = std::to_string(::getpid());
    SkillManager emptyMgr(
        "/tmp/a0_test_dep_res_empty_skills_" + pid,
        "/tmp/a0_test_dep_res_empty_store_" + pid,
        nullptr
    );
    DefaultDependencyResolver emptyRes(&emptyMgr);
    Prompt p;
    p.name = "orphan";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"anything"};
    EXPECT_FALSE(emptyRes.checkPromptDependencies(p));
    EXPECT_EQ(emptyRes.missingDependencies(p).size(), 1u);
    fs::remove_all("/tmp/a0_test_dep_res_empty_skills_" + pid);
    fs::remove_all("/tmp/a0_test_dep_res_empty_store_" + pid);
    fs::remove_all("/tmp/a0_test_dep_res_empty_logs_" + pid);
}

TEST_F(DependencyResolverTest, DependencyNameWithSpecialChars) {
    Prompt p;
    p.name = "special";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"tool@123"};
    EXPECT_FALSE(m_resolver->checkPromptDependencies(p));
}

TEST_F(DependencyResolverTest, NullManagerDoesNotCrash) {
    DefaultDependencyResolver nullRes(nullptr);
    Prompt p;
    p.name = "safe";
    p.description = "desc";
    p.prompt = "prompt";
    p.dependencies = {"something"};
    EXPECT_FALSE(nullRes.checkPromptDependencies(p));
    auto missing = nullRes.missingDependencies(p);
    EXPECT_EQ(missing.size(), 1u);
    EXPECT_EQ(missing[0], "something");
}
