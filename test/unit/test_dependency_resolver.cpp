#include "dependency_resolver.h"
#include "component_registry.h"
#include <gtest/gtest.h>

class DependencyResolverTest : public ::testing::Test {
protected:
    FileSystemComponentRegistry registry;
    DefaultDependencyResolver* resolver;

    void SetUp() override {
        registry.addTool(Tool{"base_tool", "base", "echo", "stdin"});
        registry.addSkill(Skill{"base_skill", "desc", "prompt", {}, {}});
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
    Skill s{"test_skill", "desc", "prompt", {"base_tool", "base_skill"}, {}};
    EXPECT_TRUE(resolver->checkSkillDependencies(s));
}

TEST_F(DependencyResolverTest, SkillWithMissingDep) {
    Skill s{"broken", "desc", "prompt", {"nonexistent"}, {}};
    EXPECT_FALSE(resolver->checkSkillDependencies(s));
}

TEST_F(DependencyResolverTest, MissingDepsList) {
    Skill s{"check", "desc", "prompt", {"base_tool", "missing_a", "missing_b"}, {}};
    auto missing = resolver->missingDependencies(s);
    EXPECT_EQ(missing.size(), 2u);
    if (missing.size() == 2) {
        EXPECT_NE(std::find(missing.begin(), missing.end(), "missing_a"), missing.end());
        EXPECT_NE(std::find(missing.begin(), missing.end(), "missing_b"), missing.end());
    }
}

TEST_F(DependencyResolverTest, SkillWithNoDependencies) {
    Skill s{"standalone", "desc", "prompt", {}, {}};
    EXPECT_TRUE(resolver->checkSkillDependencies(s));
    EXPECT_TRUE(resolver->missingDependencies(s).empty());
}

TEST_F(DependencyResolverTest, SelfReferencingDependency) {
    Skill s{"self_ref", "desc", "prompt", {"self_ref"}, {}};
    EXPECT_FALSE(resolver->checkSkillDependencies(s));
    auto missing = resolver->missingDependencies(s);
    EXPECT_EQ(missing.size(), 1u);
    if (missing.size() > 0) {
        EXPECT_EQ(missing[0], "self_ref");
    }
}

TEST_F(DependencyResolverTest, CircularDependencyReported) {
    Skill s{"circular", "desc", "prompt", {"a", "b"}, {}};
    auto missing = resolver->missingDependencies(s);
    EXPECT_EQ(missing.size(), 2u);
}

TEST_F(DependencyResolverTest, RegistryWithNoComponents) {
    FileSystemComponentRegistry emptyReg;
    DefaultDependencyResolver emptyRes(&emptyReg);
    Skill s{"orphan", "desc", "prompt", {"anything"}, {}};
    EXPECT_FALSE(emptyRes.checkSkillDependencies(s));
    EXPECT_EQ(emptyRes.missingDependencies(s).size(), 1u);
}

TEST_F(DependencyResolverTest, DependencyNameWithSpecialChars) {
    Skill s{"special", "desc", "prompt", {"tool@123"}, {}};
    EXPECT_FALSE(resolver->checkSkillDependencies(s));
}
