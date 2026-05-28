#include "schema_inference_engine.h"
#include <gtest/gtest.h>

class SchemaInferenceTest : public ::testing::Test {
protected:
    class MockProvider : public InferenceProvider {
    public:
        std::string response;
        std::string complete(const std::string& sys, const std::string& usr) override {
            (void)sys;
            (void)usr;
            return response;
        }
        void setMockUrl(const std::string& url) override { (void)url; }
    };

    MockProvider* provider;
    DefaultSchemaInferenceEngine* engine;

    void SetUp() override {
        provider = new MockProvider();
        provider->response = R"({
            "name": "inferred_tool",
            "description": "tool inferred from description",
            "command": "echo hello",
            "inputMode": "stdin"
        })";
        engine = new DefaultSchemaInferenceEngine(provider);
    }

    void TearDown() override {
        delete engine;
        delete provider;
    }
};

TEST_F(SchemaInferenceTest, InferToolReturnsTool) {
    Tool t = engine->inferTool("a tool that says hello");
    EXPECT_EQ(t.name, "inferred_tool");
    EXPECT_EQ(t.command, "echo hello");
}

TEST_F(SchemaInferenceTest, InferToolNameMatchesResponse) {
    Tool t = engine->inferTool("count lines in file");
    EXPECT_EQ(t.name, "inferred_tool");
    EXPECT_FALSE(t.description.empty());
    EXPECT_FALSE(t.command.empty());
}

TEST_F(SchemaInferenceTest, InferSkillReturnsSkill) {
    provider->response = R"({
        "name": "file_finder",
        "description": "finds files",
        "prompt": "search for files",
        "dependencies": ["bash"],
        "validators": []
    })";
    Skill s = engine->inferSkill("find files matching a pattern");
    EXPECT_EQ(s.name, "file_finder");
    EXPECT_EQ(s.prompt, "search for files");
}

TEST_F(SchemaInferenceTest, InferSkillDependenciesParsed) {
    provider->response = R"({
        "name": "processor",
        "description": "processes data",
        "prompt": "process it",
        "dependencies": ["bash", "grep"],
        "validators": [{"toolName": "extract_json"}]
    })";
    Skill s = engine->inferSkill("process some data");
    ASSERT_EQ(s.dependencies.size(), 2u);
    EXPECT_EQ(s.dependencies[0], "bash");
    EXPECT_EQ(s.dependencies[1], "grep");
}

TEST_F(SchemaInferenceTest, InvalidJsonResponse) {
    provider->response = "not json at all";
    EXPECT_THROW(engine->inferTool("something"), std::exception);
}

TEST_F(SchemaInferenceTest, EmptyDescription) {
    provider->response = R"({"name":"","description":"invalid","command":"","inputMode":"stdin"})";
    EXPECT_NO_THROW(engine->inferTool(""));
}

TEST_F(SchemaInferenceTest, ShortDescription) {
    provider->response = R"({"name":"one","description":"a","command":"ls","inputMode":"stdin"})";
    Tool t = engine->inferTool("ls");
    EXPECT_EQ(t.name, "one");
}
