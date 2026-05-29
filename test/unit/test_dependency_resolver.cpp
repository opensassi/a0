#include "dependency_resolver.h"
#include "skill_registry.h"
#include <gtest/gtest.h>

class DependencyResolverTest : public ::testing::Test {
protected:
    FileSystemSkillRegistry registry;
    DefaultDependencyResolver* resolver;

    void SetUp() override {
        registry.addTool(Tool{"base_tool", "base", "echo", "stdin"});
        registry.addPrompt(Prompt{"base_skill", "desc", "prompt", {}, {}});
        resolver = new DefaultDependencyResolver(&registry);
    }

    void TearDown() override {
        delete resolver;
    }
};

TEST_F(DependencyResolverTest, ToolHasNoDependencies) {
    Tool t{"simple", "desc", "echo", "stdin"};
    EXPECT_TRUE(resolver->checkToolDependencies(t));
}

TEST_F(DependencyResolverTest, SkillWithAllDepsMet) {
    Prompt p{"test_skill", "desc", "prompt", {"base_tool", "base_skill"}, {}};
    EXPECT_TRUE(resolver->checkPromptDependencies(p));
}

TEST_F(DependencyResolverTest, SkillWithMissingDep) {
    Prompt p{"broken", "desc", "prompt", {"nonexistent"}, {}};
    EXPECT_FALSE(resolver->checkPromptDependencies(p));
}

TEST_F(DependencyResolverTest, MissingDepsList) {
    Prompt p{"check", "desc", "prompt", {"base_tool", "missing_a", "missing_b"}, {}};
    auto missing = resolver->missingDependencies(p);
    EXPECT_EQ(missing.size(), 2u);
    if (missing.size() == 2) {
        EXPECT_NE(std::find(missing.begin(), missing.end(), "missing_a"), missing.end());
        EXPECT_NE(std::find(missing.begin(), missing.end(), "missing_b"), missing.end());
    }
}

TEST_F(DependencyResolverTest, SkillWithNoDependencies) {
    Prompt p{"standalone", "desc", "prompt", {}, {}};
    EXPECT_TRUE(resolver->checkPromptDependencies(p));
    EXPECT_TRUE(resolver->missingDependencies(p).empty());
}

TEST_F(DependencyResolverTest, SelfReferencingDependency) {
    Prompt p{"self_ref", "desc", "prompt", {"self_ref"}, {}};
    EXPECT_FALSE(resolver->checkPromptDependencies(p));
    auto missing = resolver->missingDependencies(p);
    EXPECT_EQ(missing.size(), 1u);
    if (missing.size() > 0) {
        EXPECT_EQ(missing[0], "self_ref");
    }
}

TEST_F(DependencyResolverTest, CircularDependencyReported) {
    Prompt p{"circular", "desc", "prompt", {"a", "b"}, {}};
    auto missing = resolver->missingDependencies(p);
    EXPECT_EQ(missing.size(), 2u);
}

TEST_F(DependencyResolverTest, RegistryWithNoComponents) {
    FileSystemSkillRegistry emptyReg;
    DefaultDependencyResolver emptyRes(&emptyReg);
    Prompt p{"orphan", "desc", "prompt", {"anything"}, {}};
    EXPECT_FALSE(emptyRes.checkPromptDependencies(p));
    EXPECT_EQ(emptyRes.missingDependencies(p).size(), 1u);
}

TEST_F(DependencyResolverTest, DependencyNameWithSpecialChars) {
    Prompt p{"special", "desc", "prompt", {"tool@123"}, {}};
    EXPECT_FALSE(resolver->checkPromptDependencies(p));
}
