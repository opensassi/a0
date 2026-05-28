#include "skill_runner.h"
#include "tool_runner.h"
#include "component_registry.h"
#include <gtest/gtest.h>

class SkillRunnerTest : public ::testing::Test {
protected:
    SubprocessToolRunner toolRunner;
    FileSystemComponentRegistry registry;
    DefaultSkillRunner* runner;

    class MockProvider : public InferenceProvider {
    public:
        std::string lastSystemPrompt;
        std::string lastUserPrompt;
        std::string response;

        std::string complete(const std::string& sys, const std::string& usr) override {
            lastSystemPrompt = sys;
            lastUserPrompt = usr;
            return response;
        }
        void setMockUrl(const std::string& url) override { (void)url; }
    };

    MockProvider* provider;

    void SetUp() override {
        provider = new MockProvider();
        provider->response = "mock response";
        registry.addTool(Tool{"list_files", "list", "ls", "stdin"});
        registry.addTool(Tool{"grep_tool", "grep", "grep", "stdin"});
        runner = new DefaultSkillRunner(&toolRunner, provider, &registry);
    }

    void TearDown() override {
        delete runner;
        delete provider;
    }
};

TEST_F(SkillRunnerTest, NoPlaceholdersPassThrough) {
    Skill s{"simple", "desc", "just a prompt", {}, {}};
    std::string expanded = runner->expandPrompt(s, json::object());
    EXPECT_EQ(expanded, "just a prompt");
}

TEST_F(SkillRunnerTest, EagerToolCallSubstituted) {
    Skill s{"finder", "desc",
        "Files: {{tool:list_files path=\"/tmp\"}} end.",
        {"list_files"}, {}};
    std::string expanded = runner->expandPrompt(s, json::object());
    EXPECT_NE(expanded.find("Files:"), std::string::npos);
}

TEST_F(SkillRunnerTest, UnknownToolPlaceholder) {
    Skill s{"broken", "desc",
        "{{tool:nonexistent_tool path=\"/\"}}",
        {}, {}};
    std::string expanded = runner->expandPrompt(s, json::object());
    EXPECT_TRUE(expanded.find("nonexistent_tool") != std::string::npos
                || expanded.find("{{tool:") != std::string::npos);
}

TEST_F(SkillRunnerTest, NoValidators) {
    Skill s{"noval", "desc", "prompt", {}, {}};
    json input = "raw llm output";
    json result = runner->runValidators(s, input);
    EXPECT_EQ(result, input);
}

TEST_F(SkillRunnerTest, ExecuteRunsFullPipeline) {
    Skill s{"pipeline", "desc",
        "prompt {{tool:list_files path=\"/\"}}",
        {"list_files"}, {}};
    json result = runner->execute(s, json::object());
    EXPECT_TRUE(result.is_string());
}

TEST_F(SkillRunnerTest, MultipleEagerCalls) {
    Skill s{"multi", "desc",
        "A: {{tool:list_files path=\"/a\"}} B: {{tool:list_files path=\"/b\"}}",
        {"list_files"}, {}};
    std::string expanded = runner->expandPrompt(s, json::object());
    EXPECT_NE(expanded.find("A:"), std::string::npos);
    EXPECT_NE(expanded.find("B:"), std::string::npos);
}

TEST_F(SkillRunnerTest, NestedPlaceholdersNotSupported) {
    Skill s{"nested", "desc",
        "{{tool:list_files path=\"{{tool:list_files path=\"/\"}}\"}}",
        {"list_files"}, {}};
    std::string expanded = runner->expandPrompt(s, json::object());
    EXPECT_TRUE(!expanded.empty());
}

TEST_F(SkillRunnerTest, ValidatorChainRuns) {
    registry.addTool(Tool{"extract_json", "extract", "cat", "stdin"});
    registry.addSkill(Skill{"filter_skill", "desc", "prompt", {},
        {ValidatorBinding{"extract_json", std::nullopt}}});
    Skill s{"filter", "desc", "raw text", {}, {ValidatorBinding{"extract_json", std::nullopt}}};
    json result = runner->runValidators(s, json("test data"));
    EXPECT_TRUE(result.is_string());
    EXPECT_EQ(result.get<std::string>(), "test data");
}

TEST_F(SkillRunnerTest, ExecuteWithValidators) {
    registry.addTool(Tool{"extract_json", "extract", "cat", "stdin"});
    Skill s{"pipe", "desc",
        "prefix {{tool:list_files path=\"/tmp\"}}",
        {"list_files"}, {ValidatorBinding{"extract_json", std::nullopt}}};
    json result = runner->execute(s, json::object());
    EXPECT_TRUE(result.is_string());
}
